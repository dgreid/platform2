// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/static_ip_parameters.h"

#include <string.h>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/error.h"
#include "shill/logging.h"
#include "shill/net/ip_address.h"
#include "shill/property_accessor.h"
#include "shill/property_store.h"
#include "shill/store_interface.h"

using std::string;
using std::vector;

namespace shill {

// static
const char StaticIPParameters::kConfigKeyPrefix[] = "StaticIP.";
// static
const char StaticIPParameters::kSavedConfigKeyPrefix[] = "SavedIP.";
// static
const StaticIPParameters::Property StaticIPParameters::kProperties[] = {
    {kAddressProperty, Property::kTypeString},
    {kGatewayProperty, Property::kTypeString},
    {kMtuProperty, Property::kTypeInt32},
    {kNameServersProperty, Property::kTypeStrings},
    {kSearchDomainsProperty, Property::kTypeStrings},
    {kPeerAddressProperty, Property::kTypeString},
    {kPrefixlenProperty, Property::kTypeInt32},
    {kIncludedRoutesProperty, Property::kTypeStrings},
    {kExcludedRoutesProperty, Property::kTypeStrings},
};

StaticIPParameters::StaticIPParameters() = default;

StaticIPParameters::~StaticIPParameters() = default;

void StaticIPParameters::PlumbPropertyStore(PropertyStore* store) {
  // Register KeyValueStore for both static ip and saved ip parameters.
  store->RegisterDerivedKeyValueStore(
      kSavedIPConfigProperty,
      KeyValueStoreAccessor(
          new CustomAccessor<StaticIPParameters, KeyValueStore>(
              this, &StaticIPParameters::GetSavedIPConfig, nullptr)));
  store->RegisterDerivedKeyValueStore(
      kStaticIPConfigProperty,
      KeyValueStoreAccessor(
          new CustomAccessor<StaticIPParameters, KeyValueStore>(
              this, &StaticIPParameters::GetStaticIPConfig,
              &StaticIPParameters::SetStaticIP)));
}

void StaticIPParameters::Load(StoreInterface* storage,
                              const string& storage_id) {
  KeyValueStore args;
  for (const auto& property : kProperties) {
    const string name(string(kConfigKeyPrefix) + property.name);
    switch (property.type) {
      case Property::kTypeInt32: {
        int32_t value;
        if (storage->GetInt(storage_id, name, &value)) {
          args.SetInt(property.name, value);
        } else {
          args.Remove(property.name);
        }
      } break;
      case Property::kTypeString: {
        string value;
        if (storage->GetString(storage_id, name, &value)) {
          args.SetString(property.name, value);
        } else {
          args.Remove(property.name);
        }
      } break;
      case Property::kTypeStrings: {
        // Name servers field is stored in storage as comma separated string.
        // Keep it as is to be backward compatible.
        string value;
        if (storage->GetString(storage_id, name, &value)) {
          vector<string> string_list = base::SplitString(
              value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
          args.SetStrings(property.name, string_list);
        } else {
          args.Remove(property.name);
        }
      } break;
      default:
        NOTIMPLEMENTED();
        break;
    }
  }
  SetStaticIP(args, nullptr);
}

void StaticIPParameters::Save(StoreInterface* storage,
                              const string& storage_id) {
  for (const auto& property : kProperties) {
    const string name(string(kConfigKeyPrefix) + property.name);
    bool property_exists = false;
    switch (property.type) {
      case Property::kTypeInt32:
        if (args_.Contains<int32_t>(property.name)) {
          property_exists = true;
          storage->SetInt(storage_id, name, args_.GetInt(property.name));
        }
        break;
      case Property::kTypeString:
        if (args_.Contains<string>(property.name)) {
          property_exists = true;
          storage->SetString(storage_id, name, args_.GetString(property.name));
        }
        break;
      case Property::kTypeStrings:
        if (args_.Contains<Strings>(property.name)) {
          property_exists = true;
          // Name servers field is stored in storage as comma separated string.
          // Keep it as is to be backward compatible.
          storage->SetString(
              storage_id, name,
              base::JoinString(args_.GetStrings(property.name), ","));
        }
        break;
      default:
        NOTIMPLEMENTED();
        break;
    }
    if (!property_exists) {
      storage->DeleteKey(storage_id, name);
    }
  }
}

void StaticIPParameters::ApplyInt(const string& property, int32_t* value_out) {
  saved_args_.SetInt(property, *value_out);
  if (args_.Contains<int32_t>(property)) {
    *value_out = args_.GetInt(property);
  }
}

void StaticIPParameters::ApplyString(const string& property,
                                     string* value_out) {
  saved_args_.SetString(property, *value_out);
  if (args_.Contains<string>(property)) {
    *value_out = args_.GetString(property);
  }
}

void StaticIPParameters::ApplyStrings(const string& property,
                                      vector<string>* value_out) {
  saved_args_.SetStrings(property, *value_out);
  if (args_.Contains<Strings>(property)) {
    *value_out = args_.GetStrings(property);
  }
}

void StaticIPParameters::RestoreStrings(const string& property,
                                        vector<string>* value_out) {
  if (saved_args_.Contains<Strings>(property)) {
    *value_out = saved_args_.GetStrings(property);
  } else {
    value_out->clear();
  }
}

void StaticIPParameters::ParseRoutes(const vector<string>& route_list,
                                     const string& gateway,
                                     vector<IPConfig::Route>* value_out) {
  IPAddress gateway_ip(gateway);
  if (gateway_ip.family() == IPAddress::kFamilyUnknown) {
    return;
  }

  for (const auto& ip : route_list) {
    IPAddress dst_ip(gateway_ip.family());
    if (!dst_ip.SetAddressAndPrefixFromString(ip)) {
      return;
    }

    IPConfig::Route route;
    dst_ip.IntoString(&route.host);
    route.prefix = dst_ip.prefix();
    route.gateway = gateway;
    value_out->push_back(route);
  }
}

void StaticIPParameters::ApplyRoutes(const string& property,
                                     const string& gateway,
                                     vector<IPConfig::Route>* value_out) {
  vector<string> saved_routes;
  for (const auto& route : *value_out) {
    saved_routes.push_back(route.host + "/" +
                           base::NumberToString(route.prefix));
  }
  saved_args_.SetStrings(property, saved_routes);

  if (!args_.Contains<Strings>(property)) {
    return;
  }
  value_out->clear();
  ParseRoutes(args_.GetStrings(property), gateway, value_out);
}

void StaticIPParameters::RestoreRoutes(const string& property,
                                       const string& gateway,
                                       vector<IPConfig::Route>* value_out) {
  value_out->clear();
  if (saved_args_.Contains<Strings>(property)) {
    ParseRoutes(saved_args_.GetStrings(property), gateway, value_out);
  }
}

void StaticIPParameters::ApplyTo(IPConfig::Properties* props) {
  if (props->address_family == IPAddress::kFamilyUnknown) {
    // In situations where no address is supplied (bad or missing DHCP config)
    // supply an address family ourselves.
    // TODO(pstew): Guess from the address values.
    props->address_family = IPAddress::kFamilyIPv4;
  }
  ClearSavedParameters();
  ApplyString(kAddressProperty, &props->address);
  ApplyString(kGatewayProperty, &props->gateway);
  ApplyInt(kMtuProperty, &props->mtu);
  ApplyStrings(kNameServersProperty, &props->dns_servers);
  ApplyStrings(kSearchDomainsProperty, &props->domain_search);
  ApplyString(kPeerAddressProperty, &props->peer_address);
  ApplyInt(kPrefixlenProperty, &props->subnet_prefix);
  ApplyStrings(kExcludedRoutesProperty, &props->exclusion_list);
  ApplyRoutes(kIncludedRoutesProperty, props->gateway, &props->routes);
}

void StaticIPParameters::RestoreTo(IPConfig::Properties* props) {
  props->address = saved_args_.LookupString(kAddressProperty, "");
  props->gateway = saved_args_.LookupString(kGatewayProperty, "");
  props->mtu = saved_args_.LookupInt(kMtuProperty, 0);
  RestoreStrings(kNameServersProperty, &props->dns_servers);
  RestoreStrings(kSearchDomainsProperty, &props->domain_search);
  props->peer_address = saved_args_.LookupString(kPeerAddressProperty, "");
  props->subnet_prefix = saved_args_.LookupInt(kPrefixlenProperty, 0);
  RestoreStrings(kExcludedRoutesProperty, &props->exclusion_list);
  RestoreRoutes(kIncludedRoutesProperty, props->gateway, &props->routes);
  ClearSavedParameters();
}

void StaticIPParameters::ClearSavedParameters() {
  saved_args_.Clear();
}

bool StaticIPParameters::ContainsAddress() const {
  return args_.Contains<string>(kAddressProperty) &&
         args_.Contains<int32_t>(kPrefixlenProperty);
}

bool StaticIPParameters::ContainsNameServers() const {
  return args_.Contains<Strings>(kNameServersProperty);
}

KeyValueStore StaticIPParameters::GetSavedIPConfig(Error* /*error*/) {
  return saved_args_;
}

KeyValueStore StaticIPParameters::GetStaticIPConfig(Error* /*error*/) {
  return args_;
}

bool StaticIPParameters::SetStaticIP(const KeyValueStore& value,
                                     Error* /*error*/) {
  if (args_ == value) {
    return false;
  }
  args_ = value;
  if (ipconfig_) {
    ipconfig_->Refresh();
  }
  return true;
}

void StaticIPParameters::SetIPConfig(base::WeakPtr<IPConfig> ipconfig) {
  ipconfig_ = ipconfig;
}

}  // namespace shill
