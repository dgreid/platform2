# NET > NTLM

This directory contains the net NTLM library which is a modified copy of
Chromium's net NTLM code (net/ntlm). The current files are taken from milestone
M88 of the code with the latest commit hash of
`2fa09e20ad9e4a88207418cdffe83fd244ad6151`.

The net NTLM library is used by System-proxy to generate NTLM authentication
messages using the Chrome OS login password.

## Modifications

The code here is a modification of Chromium's net NTLM code. The modification is
done to minimize the code imported. Currently, we're only interested in the
`NtlmClient::GenerateAuthenticateMessage` method which is the entry point to
the net NTLM stack.

The modification process is done by importing the chromium //net/ntlm code with
the original directory structure and the required changes to be compliant with the
current clang presubmit checks (see CL:2532227), followed by CL:2532006
containing the minimal amount of code necessary to build the code and successfully
run the unit test.

To verify that the build is successful, run the tests by entering the following
command in `cros_sdk`:

```shell
P2_TEST_FILTER="Ntlm*" USE="-cros-debug" cros_workon_make --board=eve system-proxy --test
```

Below are the changes made:
*   Replace boringssl with openssl. boringssl is not available on the platform.
*   Remove reference to the "net_string_util.h" file because it requires the
third_party library icu to build. Instead, use the locale insensitive
`base::ToUpperASCII` moethod to converts UTF-16 strings to uppercase. This may
cause issues since the browser does case sensitive conversions
(https://crbug.com/1051924).
*   For the test data, explicitly convert hex codes to signed char. On the
platform, the hex codes are converted to unsigned chars by default, causing
compilation issues when the value is re-assigned to a signed char.
*   Redefine the `NET_EXPORT` and `NET_EXPORT_PRIVATE` macros to do nothing. These
 macros are not necessary since the //net/ntlm code is not built as a component
 build for System-proxy.
*   Fix clang presubmit errors (see CL:2532227):
    *   Update license headers and header guards.
    *   Add missing includes.
    *   Replace `DISALLOW_COPY_AND_ASSIGN` macro with explicitly deteled constructors.
    *   Remove file `/net/ntlm/ntlm_client_fuzzer.cc`.
    *   Format files `system-proxy/net/ntlm/ntlm.h` and `system-proxy/net/ntlm/ntlm_test_data.h`.
    *   Rename *_unittest.cc files to *_test.cc.
    An alternative would have been to disable presubmit tests from running on the
    imported files by modifying the `//platform2/PRESUBMIT.cfg` file and the associated
    scripts which is more complex task than fixing the errors.
