# StoryFlow

StoryFlow is a native Linux editor for writing fiction. The first build supports
bold and italic text, HTML documents, undo and redo, and a fixed inline completion.

## Build on Debian or Ubuntu

```sh
sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev libxml2-dev
cmake -S . -B build -G Ninja
cmake --build build
./build/storyflow
```

Run the document tests with `ctest --test-dir build --output-on-failure` from a
graphical session, or prefix the command with `xvfb-run -a` in a headless session.

## Try it

- Use **Ctrl+B** and **Ctrl+I** to format a selection or toggle formatting before typing.
- Use **Ctrl+O**, **Ctrl+S**, and **Ctrl+Shift+S** to open, save, or save a copy as HTML.
- Type `The door` at the end of a line. Use **Tab** to accept the suggestion,
  **Alt+Right** to accept its next word, or **Esc** to dismiss it.
- Use **Ctrl+Z** to undo and **Ctrl+Shift+Z** or **Ctrl+Y** to redo.

HTML import preserves text and bold and italic markup. The completion is a
fixed example for testing the editing interaction.

See [the implementation plan](docs/Plan.md) for the remaining milestones.
