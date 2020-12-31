// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generates a libsirenia::rpc implementation for marked traits.

#![recursion_limit = "128"]

extern crate proc_macro;

use std::env;
use std::result::Result as StdResult;

use proc_macro2::{Span, TokenStream};
use quote::{format_ident, quote};
use syn::parse::{Error, Result};
use syn::{
    parse_macro_input, FnArg, Ident, ItemTrait, PathArguments, ReturnType, Signature, TraitItem,
    TraitItemType, Type,
};

/// Links the macro to the actual implementation.
#[proc_macro_attribute]
pub fn sirenia_rpc(
    _args: proc_macro::TokenStream,
    input: proc_macro::TokenStream,
) -> proc_macro::TokenStream {
    let item_trait = parse_macro_input!(input as ItemTrait);

    let expanded = sirenia_rpc_impl(&item_trait).unwrap_or_else(|err| {
        let compile_error = err.to_compile_error();
        quote! {
            #compile_error

            // Include the original input to avoid "use of undeclared type"
            // errors elsewhere.
            #item_trait
        }
    });

    expanded.into()
}

// Allow for importing from libsirenia when the macro is used inside or outside libsirenia.
fn get_libsirenia_prefix() -> TokenStream {
    if env::var("CARGO_PKG_NAME")
        .map(|pkg| &pkg == "libsirenia")
        .unwrap_or(false)
    {
        quote! {crate}
    } else {
        quote! {::libsirenia}
    }
}

/// Match trait type with "type Error;" and return an error if it has attributes, trait bounds,
/// generics.
fn is_error_type(item: &TraitItemType) -> Result<bool> {
    if item.ident == "Error" {
        if item.attrs.is_empty() && item.colon_token.is_none() && item.generics.params.is_empty() {
            Ok(true)
        } else {
            Err(Error::new(
                Span::call_site(),
                "'type Error' should not have attributes, generics, or bounds",
            ))
        }
    } else {
        Err(Error::new(
            Span::call_site(),
            format!("found a type other than 'Error': {}", item.ident),
        ))
    }
}

/// Converts 'snake_case' to upper camel case. Upper case letters in the input will be preserved.
fn to_upper_camel_case(snake_case: &str) -> String {
    let mut output = String::new();
    let mut next_upper = true;
    output.reserve(snake_case.len());
    for c in snake_case.chars() {
        if c == '_' {
            next_upper = true;
        } else if next_upper {
            output.push(c.to_ascii_uppercase());
            next_upper = false;
        } else {
            output.push(c);
        }
    }
    output
}

/// Extract the first generic type T in Result<T, _> used for Ok(T).
fn extract_ok_type(type_value: &Type) -> StdResult<TokenStream, ()> {
    match type_value {
        Type::Path(path) => {
            let path = path.path.segments.last().ok_or(())?;
            // Aliases make it so the identifier might not be "Result".
            if let PathArguments::AngleBracketed(arg_tokens) = &path.arguments {
                let args = arg_tokens.args.first().ok_or(())?;
                Ok(quote! {#args})
            } else {
                Err(())
            }
        }
        _ => Err(()),
    }
}

struct RpcMethodHelper {
    signature: Signature,
    enum_name: Ident,
    request_args: Vec<FnArg>,
    request_arg_names: Vec<TokenStream>,
    response_arg: TokenStream,
}

impl RpcMethodHelper {
    fn new(signature: Signature) -> Result<Self> {
        let str_ident = signature.ident.to_string();
        let args = signature.inputs.iter();
        let response_arg = match &signature.output {
            ReturnType::Default => {
                quote! {}
            }
            ReturnType::Type(_, arg) => extract_ok_type(arg).map_err(|_| {
                Error::new(
                    Span::call_site(),
                    format!(
                        "unable to parse return type for '{}' expected std::result::Result",
                        &str_ident
                    ),
                )
            })?,
        };

        let mut request_args: Vec<FnArg> = Vec::new();
        let mut request_arg_names: Vec<TokenStream> = Vec::new();
        let mut has_self = false;
        for arg in args {
            match arg {
                FnArg::Receiver(_) => {
                    //TODO decide if self is ok (as opposed to &self and &mut self).
                    has_self = true;
                }
                FnArg::Typed(pat_type) => {
                    request_args.push(arg.clone());
                    let name = &pat_type.pat;
                    request_arg_names.push(quote! {#name});
                }
            }
        }
        if !has_self {
            return Err(Error::new(
                Span::call_site(),
                format!("'{}' needs a '&self' argument", &str_ident),
            ));
        }

        Ok(RpcMethodHelper {
            signature,
            enum_name: format_ident!("{}", to_upper_camel_case(&str_ident)),
            request_args,
            request_arg_names,
            response_arg,
        })
    }

    fn get_request_enum_item(&self) -> TokenStream {
        let enum_name = &self.enum_name;
        let args = &self.request_args;
        quote! {
            #enum_name{#(#args),*}
        }
    }

    fn get_response_enum_item(&self) -> TokenStream {
        let enum_name = &self.enum_name;
        let response_arg = &self.response_arg;
        quote! {
            #enum_name(#response_arg)
        }
    }

    fn get_client_trait_impl(
        &self,
        libsirenia_prefix: &TokenStream,
        request_name: &Ident,
        response_name: &Ident,
    ) -> TokenStream {
        let signature = &self.signature;
        let enum_name = &self.enum_name;
        let request_arg_names = &self.request_arg_names;
        quote! {
            #signature {
                match #libsirenia_prefix::rpc::Invoker::<Self>::invoke(
                    ::std::ops::DerefMut::deref_mut(&mut self.transport.borrow_mut()),
                    #request_name::#enum_name{#(#request_arg_names),*},
                ) {
                    Ok(#response_name::#enum_name(x)) => Ok(x),
                    Err(e) => Err(#libsirenia_prefix::rpc::Error::Communication(e)),
                    _ => Err(#libsirenia_prefix::rpc::Error::ResponseMismatch),
                }
            }
        }
    }

    fn get_handle_message_impl(&self, request_name: &Ident, response_name: &Ident) -> TokenStream {
        let function_name = &self.signature.ident;
        let enum_name = &self.enum_name;
        let request_arg_names = &self.request_arg_names;
        quote! {
            #request_name::#enum_name{#(#request_arg_names),*} => {
                self.#function_name(#(#request_arg_names),*).map(|x| #response_name::#enum_name(x))
            }
        }
    }
}

fn sirenia_rpc_impl(item_trait: &ItemTrait) -> Result<TokenStream> {
    let trait_name = &item_trait.ident;
    let mut found_error_type = false;

    let mut rpc_method_helpers: Vec<RpcMethodHelper> = Vec::new();
    for x in item_trait.items.as_slice() {
        match x {
            TraitItem::Type(trait_item_type) => {
                if is_error_type(trait_item_type)? {
                    found_error_type = true;
                }
            }
            TraitItem::Method(trait_item_method) => {
                rpc_method_helpers.push(RpcMethodHelper::new(trait_item_method.sig.clone())?);
            }
            _ => {}
        }
    }
    if !found_error_type {
        return Err(Error::new(
            Span::call_site(),
            "trait needs to define 'type Error'",
        ));
    }

    let libsirenia_prefix = get_libsirenia_prefix();

    let request_name = format_ident!("{}Request", trait_name);
    let response_name = format_ident!("{}Response", trait_name);
    let client_struct_name = format_ident!("{}Client", trait_name);
    let server_trait_name = format_ident!("{}Server", trait_name);

    let request_contents: Vec<TokenStream> = rpc_method_helpers
        .iter()
        .map(|x| x.get_request_enum_item())
        .collect();
    let response_contents: Vec<TokenStream> = rpc_method_helpers
        .iter()
        .map(|x| x.get_response_enum_item())
        .collect();
    let client_trait_contents: Vec<TokenStream> = rpc_method_helpers
        .iter()
        .map(|x| x.get_client_trait_impl(&libsirenia_prefix, &request_name, &response_name))
        .collect();
    let handle_message_contents: Vec<TokenStream> = rpc_method_helpers
        .iter()
        .map(|x| x.get_handle_message_impl(&request_name, &response_name))
        .collect();

    Ok(quote! {
        #item_trait

        #[derive(::serde::Deserialize, ::serde::Serialize)]
        pub enum #request_name {
            #(#request_contents,)*
        }

        #[derive(::serde::Deserialize, ::serde::Serialize)]
        pub enum #response_name {
            #(#response_contents,)*
        }

        pub struct #client_struct_name {
            transport: ::std::cell::RefCell<#libsirenia_prefix::transport::Transport>,
        }

        impl #client_struct_name {
            pub fn new(transport: #libsirenia_prefix::transport::Transport) -> Self {
                #client_struct_name {
                    transport: ::std::cell::RefCell::new(transport),
                }
            }
        }

        impl #trait_name for #client_struct_name {
            type Error = #libsirenia_prefix::rpc::Error;

            #(#client_trait_contents)*
        }

        impl #libsirenia_prefix::rpc::Procedure for #client_struct_name {
            type Request = #request_name;
            type Response = #response_name;
        }

        pub trait #server_trait_name: #trait_name<Error = ()> {
            fn box_clone(&self) -> Box<dyn #server_trait_name>;
        }

        impl<T: #trait_name<Error = ()> + ::std::clone::Clone + 'static> #server_trait_name for T {
            fn box_clone(&self) -> ::std::boxed::Box<dyn #server_trait_name> {
                ::std::boxed::Box::new(self.clone())
            }
        }

        impl #libsirenia_prefix::rpc::Procedure for Box<dyn #server_trait_name> {
            type Request = #request_name;
            type Response = #response_name;
        }

        impl #libsirenia_prefix::rpc::MessageHandler for Box<dyn #server_trait_name> {
            fn handle_message(&self, request: #request_name) -> ::std::result::Result<#response_name, ()> {
                match request {
                    #(#handle_message_contents)*
                }
            }
        }

        impl ::std::clone::Clone for ::std::boxed::Box<dyn #server_trait_name> {
            fn clone(&self) -> Self {
                self.box_clone()
            }
        }
    })
}
