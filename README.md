# StoryFlow

StoryFlow is a native Linux editor for writing fiction. The first build supports
bold and italic text, HTML documents, undo and redo, and offline inline word completion.

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
- Type `whis` to try word completion. Use **Tab** or **Alt+Right** to accept
  the suggested ending, or **Esc** to dismiss it. Inline suggestions move to a
  small label at the caret when there is existing text or insufficient space.
- Use **Ctrl+Z** to undo and **Ctrl+Shift+Z** or **Ctrl+Y** to redo.

HTML import preserves text and bold and italic markup. Completion starts after
three letters and ranks words already used in the current draft first. Draft
vocabulary is calculated in memory and stays in the editor.

The built-in English vocabulary contains 7,789 words with frequency counts from
the public-domain [Project Gutenberg selections distributed by NLTK](https://github.com/nltk/nltk_data/blob/gh-pages/index.xml).
The table counts alphabetic ASCII tokens of at least four letters, folded to
lowercase and seen at least seven times in the Austen, Bryant, Burgess, Carroll,
Chesterton, Edgeworth, and Melville files.

See [the implementation plan](docs/Plan.md) for the remaining milestones.
