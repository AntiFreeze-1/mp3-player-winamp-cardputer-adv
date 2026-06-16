Import("env")
import os

# ESP32-audioI2S 3.0.0 has a dangling-else bug in processLocalFile() /
# processWebFile() / processWebStream(): `compression` is set to 1 for
# CODEC_WAV, but the very next "if(m_codec == CODEC_FLAC) ... else ..."
# is NOT chained to the WAV check, so the bare `else` unconditionally
# overwrites compression back to 3 for any non-FLAC codec, including WAV.
# This throttles WAV PCM playback to one playAudioData() call every 3
# loop passes instead of every pass, starving the output pipeline and
# causing audible pops/scratches during WAV playback. Patch it to a
# proper if/else-if/else chain.
PROJECT_DIR = env.get("PROJECT_DIR")
lib_path = os.path.join(
    PROJECT_DIR, ".pio", "libdeps", env["PIOENV"],
    "ESP32-audioI2S-master", "src", "Audio.cpp",
)

OLD = """        uint8_t compression;
        if(m_codec == CODEC_WAV)  compression = 1;
        if(m_codec == CODEC_FLAC) compression = 2;
        else compression = 3;"""

NEW = """        uint8_t compression;
        if(m_codec == CODEC_WAV)       compression = 1;
        else if(m_codec == CODEC_FLAC) compression = 2;
        else                            compression = 3;"""

if os.path.isfile(lib_path):
    with open(lib_path, "r") as f:
        content = f.read()
    n = content.count(OLD)
    if n:
        content = content.replace(OLD, NEW)
        with open(lib_path, "w") as f:
            f.write(content)
        print(f"[patch_libs] Patched {n} occurrence(s) of the WAV compression bug in {lib_path}")
    elif NEW not in content:
        print(f"[patch_libs] WARNING: expected pattern not found in {lib_path} (library version may have changed)")
