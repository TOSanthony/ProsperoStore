// ps5-native-app-boilerplate - Certificate import ABI and input boundary checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../examples/https-trust/https_trust.hpp"
#include <cassert>
#include <cstddef>
#include <string>
struct Certificate
{
    const void *data;
    std::size_t size;
};
int imports = 0;
extern "C" int sceHttpsLoadCert(int context, int count, const Certificate *const *roots,
                                const Certificate *client, const Certificate *key)
{
    assert(context == 7 && count == 2 && !client && !key);
    for (int i = 0; i < count; ++i)
    {
        const std::string_view pem(static_cast<const char *>(roots[i]->data), roots[i]->size);
        assert(pem.starts_with("-----BEGIN CERTIFICATE-----") &&
               pem.ends_with("-----END CERTIFICATE-----"));
    }
    ++imports;
    return 123;
}
int main()
{
    const std::string cert = "-----BEGIN CERTIFICATE-----\nAQ==\n-----END CERTIFICATE-----";
    assert(https_trust::load_pem_roots(7, "# system roots\n" + cert + "\r\n" + cert) == 123);
    assert(imports == 1);
    assert(https_trust::load_pem_roots(7, "") < 0);
    assert(https_trust::load_pem_roots(7, cert.substr(0, cert.size() - 1)) < 0);
    assert(https_trust::load_pem_roots(7, std::string(512 * 1024 + 1, 'x')) < 0);
    assert(https_trust::load_pem_roots(7, cert + std::string(1, '\0')) < 0);
    std::string many;
    for (int i = 0; i < 257; ++i)
        many += cert;
    assert(https_trust::load_pem_roots(7, many) < 0);
    assert(imports == 1);
}
