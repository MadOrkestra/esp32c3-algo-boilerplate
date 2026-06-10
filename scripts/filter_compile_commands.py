#!/usr/bin/env python3
import json
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
DB_PATH = PROJECT_DIR / "compile_commands.json"


def main() -> None:
    with DB_PATH.open(encoding="utf-8") as handle:
        entries = json.load(handle)

    filtered = []
    for entry in entries:
        file_path = Path(entry["file"])
        if not file_path.is_absolute():
            file_path = (PROJECT_DIR / file_path).resolve()
        try:
            rel = file_path.relative_to(PROJECT_DIR)
        except ValueError:
            continue
        if len(rel.parts) >= 2 and rel.parts[0] == "src" and rel.suffix == ".cpp":
            filtered.append(entry)

    with DB_PATH.open("w", encoding="utf-8") as handle:
        json.dump(filtered, handle, indent=4)
        handle.write("\n")

    print(f"Filtered compile_commands.json: {len(entries)} -> {len(filtered)} entries")


if __name__ == "__main__":
    main()
