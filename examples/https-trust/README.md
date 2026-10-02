# HTTPS after filesystem elevation

The SSL library's normal sandbox-relative certificate path can become
unreachable after elevation. Keep certificate verification enabled. Call
`https_trust::load_builtin_roots(ssl_context, http_context)` on a worker after
`sceHttpInit`, before making templates or requests. It obtains the platform's
built-in DER roots through `sceSslGetCaCerts`, encodes them as PEM, releases
the platform allocation and imports them through `sceHttpsLoadCert`.
`load_pem_roots` also accepts an existing trusted PEM bundle. Never substitute
a network-supplied CA file. The on-disk `CA_LIST.cer` may contain a different
set of roots from the library's built-in store.

The helper bounds input to 512 KiB, 256 roots, and 16 KiB per PEM certificate
(12 KiB per built-in DER certificate);
the platform validates each certificate and copies its data during the call.
Use a sufficiently sized SSL pool for the explicit imports (2 MiB in the
ProsperoStore adopter). All hostname, validity, CA and signature checks remain
enabled. A parsing or platform error must stop the request.

`make test-https-trust` checks the API layout and input limits under sanitizers.
The public five-argument API and its pointer/size certificate records were
cross-checked against the local module's export and argument handling. Native
validation belongs to each adopter's console evidence.
