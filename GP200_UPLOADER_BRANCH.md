# GP-200 Uploader branch

This branch adds a third top-level tab, `GP-200 Uploader`, to the existing backend tab control.

## Files added
- `src/gp200_clo_upload.hpp/.cpp`: Sound Clone validation, 2048->1024 preparation, checksum and SysEx chunk builder ported from the VST implementation without JUCE.
- `src/gp200_midi.hpp/.cpp`: native Windows WinMM MIDI detection, handshake, SysEx transport and completion ACK handling.

## Files modified
- `src/gui.cpp`: third tab UI, CLO picker, SnapTone 1-10 selector, MIDI rescan, progress bar, drag/drop and threaded upload.
- `CMakeLists.txt`: adds uploader sources and links `winmm`.

## Transfer sequence
1. Open matching GP-200/Valeton MIDI IN and OUT ports.
2. Identity query.
3. Wait for GP-200 0x12/0x08 identity response.
4. Enter Editor Mode.
5. Wait 100 ms.
6. Send Sound Clone prepare message for selected AMP/DIST slot.
7. Wait 100 ms.
8. Send 183-byte source chunks with 30 ms spacing.
9. Do not send a commit packet.
10. Wait up to 2000 ms for the observed 0x12/0x0C completion ACK matching the selected global slot.

The VST's five-chunk state dump is not sent because this standalone uploader does not need current preset/editor state; only Editor Mode is needed before the write transaction.
