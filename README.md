= Hugin

Hugin is a command line util able to generate small delta software updates, easy to install in-place on constrained devices. Hugin can then be used to authentificate the update package, can distribute it to devices. Munin offer a reference implementation of the code necessary on the device to install the update package, in place, in a resilient manner.

Properly used, Hugin and Munin enable secure and efficient firmware updates of very small IoT devices.

= Build

== Hugin

- Run cmake to create Makefiles: `cmake .`
- Make: `make Hugin`
- Hugin is now built in `hugin/`

== How to use

=== Generate an update

Hugin can generate update packages through two ways.

- The first generate a single update, using the following command: `path/to/Hugin diff -v1 path/to/old/firmware/image -v2 path/to/new/firmware/image -o path/to/output/directory/`

- The second generate update packages for many firmware images. This approach require a config file, such as the sample in `test_files`. This mode is used with the following command: `path/to/Hugin diff --batchMode --config test_files/config.json -o path/to/output/directory/`

=== Sign an update

This step require access to the device master key. This cryptographic key is EXTREMELY powerful and thus should be stored on a secure computer, hopefully an HSM. At the very least, it is strongly recommanded to perform the signing on a dedicated, air-gapped server.
Assuming this is the case, this is done, the following command will make Hugin sign the various update packages: `path/to/Hugin authenticate -p path/to/update/directory/ -o path/to/signed/output/directory/ -k path/to/priv.key`

=== Import an update to the serveur

A small Python server implement Munin's protocol and provide various security guarantees.
Importing the updates to the server is done with the following command: `python3 hugin/webserver/zeus.py import -d <Device Name> -i path/to/signed/output/directory/ -o /path/to/server/update/storage/`

=== Launching the server

Launching the small Python server is done with the following command: `python3 hugin/webserver/zeus.py server`.
The port is configured at the top of `server.py`.

=== Generate cryptographic keys

`path/to/Hugin crypto --generateKeys path/to/private/key path/to/public/key`.

=== Test the binary

This test doesn't validate wether the binary was tampered with, only that features work as expected.

`path/to/Hugin test`.
