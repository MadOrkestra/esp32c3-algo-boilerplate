Import("env")

FILTER_SCRIPT = "$PROJECT_DIR/scripts/filter_compile_commands.py"

env.AddPostAction(
    "$BUILD_DIR/firmware.elf",
    [
        env.VerboseAction("pio run -t compiledb", "Generating compile_commands.json"),
        env.VerboseAction(f"python3 {FILTER_SCRIPT}", "Filtering compile_commands.json"),
    ],
)
