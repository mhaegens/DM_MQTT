"""Exercise real profile-store file operations using mocked ESP peripherals."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
idf = Path(os.environ["IDF_PATH"])
cjson = idf / "components/json/cJSON"
with tempfile.TemporaryDirectory(prefix="dm-store-", dir="/tmp") as fixture:
    executable = Path(fixture) / "test"
    subprocess.run([
        "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
        "-Itests/native-stubs", "-Ifirmware/main", f"-I{cjson}",
        f'-DTEST_BASE="{fixture}"',
        f'-DCONFIG_STORE_SD_PATH="{fixture}/sd"',
        f'-DCONFIG_STORE_FLASH_PATH="{fixture}/flash"',
        "tests/test_config_store.c", "firmware/main/config_store.c",
        "firmware/main/config_validation.c", str(cjson / "cJSON.c"),
        "-o", str(executable),
    ], cwd=repo, check=True)
    subprocess.run([str(executable)], check=True)
