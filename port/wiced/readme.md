# BTstack port for WICED platform

To integrate BTstack into the WICED SDK, please move the BTstack project into WICED-SDK-X/libraries.
Then create projects for BTstack examples in WICED/apps/btstack by running:

	./create_examples.py

Now, the BTstack examples can be build from the WICED root in the same way as other examples, e.g.:

	./make btstack.spp_and_le_counter-RB_DUO

to build the SPP-and-LE-Counter example.

See WICED documentation about how to install it.

Only tested on Redbear Duo platform.

It should work with all WICED platforms that contain a Broadcom Bluetooth chipset.

The maximal baud rate is limited to 3 mbps.

The port uses the generated WIFI address plus 1 as Bluetooth MAC address.

The examples that implement a BLE Peripheral/provide a GATT Server use the GATT DB in the .gatt file.
After modifying the .gatt file, please run ./update_gatt_db.sh in the apps/btstack/$(EXAMPLE) folder.


