# Torture-test payload. The checksum (FNV-1a, see src/common.hpp) covers
# publisher_id, seq, data and text, so any corruption is detected.
uint32 publisher_id
uint64 seq
uint32 checksum
uint8[] data
string text
