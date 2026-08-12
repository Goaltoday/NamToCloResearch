# Third-party runtime

This project does not include or redistribute proprietary Hotone/Ampero files.

The application expects the following user-supplied files:

```text
runtime\ampero\HTUSBTools.dll
runtime\ampero\nam_input_wav.wav
```

These files must come from a legally obtained Ampero II installation/package and remain subject to their original license terms.

The source code in this repository dynamically loads `HTUSBTools.dll` and calls the exported `namConvertCloData` function for interoperability/research purposes.
