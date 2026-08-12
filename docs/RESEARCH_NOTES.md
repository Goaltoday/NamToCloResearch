# Research notes frozen for v1.0

The v1.0 application intentionally freezes the confirmed conversion path and removes experimental probes from the user-facing product.

Confirmed path retained:

1. `HTUSBTools.dll::namConvertCloData` generates the Ampero VTSI with a 2048-coefficient Block B.
2. The GP-200 compact serializer keeps Block A and the first 1024 Block B coefficients, updates the VTSI size/model fields, recalculates CRC16/MODBUS and zero-pads the physical file to 0x2288 bytes.

Not presented as confirmed:

- The Ampero coefficients are not byte-identical to the official Valeton coefficients.
- Real GP-200 hardware acceptance and audio behavior of the generated 1024 CLO remain to be tested.
