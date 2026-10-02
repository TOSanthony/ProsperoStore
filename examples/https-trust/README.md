# HTTPS after filesystem elevation

The SSL library's normal sandbox-relative certificate path can become
unreachable after elevation. Keep certificate verification enabled. Read the
console's existing `/system/common/cert/CA_LIST.cer` on a worker and pass its
PEM contents to `https_trust::load_pem_roots` after `sceHttpInit`, before making
templates or requests. In an ordinary sandbox the path is
`/common/cert/CA_LIST.cer`. Never substitute a network-supplied CA file.

The helper bounds input to 512 KiB, 256 roots, and 16 KiB per certificate;
the platform validates each certificate and copies its data during the call.
Use a sufficiently sized SSL pool for the explicit imports (2 MiB in the
ProsperoStore adopter). All hostname, validity, CA and signature checks remain
enabled. A parsing or platform error must stop the request.

`make test-https-trust` checks the API layout and input limits under sanitizers.
The public five-argument API and its pointer/size certificate records were
cross-checked against the local module's export and argument handling. Native
validation belongs to each adopter's console evidence.
