# **Detailed implementation plan**

## **Native Linux predictive story-writing editor**

**Working project name:** StoryFlow (provisional)\
**Platform:** Linux — XFCE, GNOME, KDE\
**Application type:** Native desktop rich-text editor\
**Primary purpose:** Minimise typing during creative writing through intelligent, context-aware text completion.

The application will combine the simplicity of a traditional text editor, the formatting capabilities needed for fiction writing, and the predictive capabilities of modern language models.

Unlike conventional AI writing applications, the author will not need to issue prompts or interact with a chatbot. Predictions will appear naturally at the cursor as the author writes.

## **1. Project objectives**

### **Primary objectives**

- Provide a lightweight, distraction-free writing environment.
- Support bold, italic and basic rich-text formatting.
- Predict partially typed words.
- Predict the next word or phrase.
- Predict complete sentences.
- Learn the author's vocabulary, recurring expressions and writing style.
- Understand characters, locations and narrative context.
- Operate offline without requiring a subscription.
- Reduce the number of keystrokes required to write a story.

### **Design principles**

| Principle                 | Implementation                                                     |
| ------------------------- | ------------------------------------------------------------------ |
| Author remains in control | Never automatically insert generated prose                         |
| Predict, don't interrupt  | Suggestions appear as faint inline text                            |
| Performance first         | Statistical predictions available immediately                      |
| Privacy                   | Local inference and local document storage                         |
| Simplicity                | No chatbot or unnecessary interface elements                       |
| Adaptability              | Learn from accepted suggestions and actual writing                 |
| Reliability               | Editor remains functional when all prediction engines are disabled |

The central performance metric should be **useful keystrokes saved**, not the amount of AI-generated content.

## **2. Proposed user interface**

**StoryFlow**

the-last-evening.story

File

Edit

Format

View

Prediction

Help

│

│

MANUSCRIPT

Chapter 1

Chapter 2

Chapter 3

STORY NOTES

Characters

Locations

Plot

# **The Last Evening**

The rain had not stopped since morning. Margaret stood at the window, watching the distant lights disappear into the mist.

*Perhaps he would never return.*

She turned away and walked towards the old wooden desk. On its surface lay an unopened letter, its edges yellowed with age.

Tab

Accept

Alt + →

Next word

Esc

Dismiss

1,248 words

Local model ready

Target application interface. The chapter sidebar and story notes are planned extensions beyond the initial MVP.

### **Editor functionality**

The main editor should provide:

- Bold, italic and underline.
- Heading and paragraph styles.
- Undo and redo.
- Adjustable font, line spacing and page width.
- Word and character counts.
- Find and replace.
- Automatic saving and crash recovery.
- Dark, light and sepia themes.
- Fullscreen and typewriter scrolling modes.
- Optional chapter navigation.
- Standard rich-text clipboard support.

No complex page layout, spreadsheet functionality, tables or desktop publishing features are required initially.

## **3. Technology stack**

| Component                | Recommended technology          |
| ------------------------ | ------------------------------- |
| Programming language     | C++17 or C++20                  |
| GUI framework            | GTK 3.24                        |
| Text editor              | GtkTextView                     |
| Rich-text formatting     | GtkTextBuffer tags              |
| Ghost-text rendering     | Pango and Cairo                 |
| Unicode processing       | ICU / GLib                      |
| Prefix prediction        | MARISA-trie                     |
| Statistical model        | KenLM                           |
| Neural inference         | llama.cpp                       |
| Model format             | GGUF                            |
| Personalisation database | SQLite                          |
| HTTP client              | libcurl                         |
| Document metadata        | JSON                            |
| Build system             | CMake                           |
| Unit testing             | Catch2                          |
| Corpus preparation       | Python                          |
| Distribution             | DEB, AppImage, optional Flatpak |

### **Why GTK3?**

GTK3 remains a reasonable choice for a lightweight XFCE-oriented application. It provides native desktop controls, good text rendering, Unicode support, clipboard integration and established custom drawing mechanisms.

GTK's text widget also exposes drawing layers that can be used to prototype non-destructive inline suggestions.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://docs.gtk.org\&sz=32)

https\://docs.gtk.org+1



GTK4 is an alternative, particularly because it provides built-in text-buffer undo and redo facilities. However, this plan retains GTK3 to prioritise integration with your existing desktop environment.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://docs.gtk.org\&sz=32)

4.0: Text Widget Overview



## **4. Overall system architecture**

The application will be organised into independent subsystems so that the prediction engine can eventually be reused in other Linux applications.

| Subsystem              | Responsibility                                     |
| ---------------------- | -------------------------------------------------- |
| Editor Core            | Text manipulation and formatting                   |
| Document Manager       | Loading, saving, recovery and export               |
| Context Manager        | Extracting relevant manuscript context             |
| Prediction Coordinator | Managing prediction requests                       |
| Lexical Engine         | Partial-word completion                            |
| Statistical Engine     | Contextual word and phrase prediction              |
| Neural Engine          | Sentence-level prediction                          |
| Ranking Engine         | Selecting useful suggestions                       |
| Learning Engine        | Updating personalised vocabulary                   |
| Suggestion Renderer    | Drawing and accepting ghost text                   |
| Story Memory           | Tracking characters, locations and narrative facts |

## **5. Rich-text editor implementation**

### **5.1 Editor core**

Use `GtkTextView` as the main editing component, with `GtkTextBuffer` maintaining the document.

Create an abstraction around GTK rather than allowing other modules to manipulate the text buffer directly.

```
class Editor {
public:
    void insertText(const std::string& text);
    void deleteSelection();

    void toggleBold();
    void toggleItalic();
    void toggleUnderline();

    void undo();
    void redo();

    std::string getText() const;
    std::string getTextBeforeCursor(size_t limit) const;
    std::string getTextAfterCursor(size_t limit) const;

    size_t getCursorPosition() const;
    uint64_t getDocumentRevision() const;

    bool hasSelection() const;
};
```

### **5.2 Rich-text formatting**

Implement formatting using named text tags.

| Tag           | Function           |
| ------------- | ------------------ |
| `bold`        | Font weight        |
| `italic`      | Italic font style  |
| `underline`   | Underlining        |
| `heading1`    | Chapter heading    |
| `heading2`    | Section heading    |
| `blockquote`  | Indented quotation |
| `scene-break` | Scene separator    |

Example:

```
gtk_text_buffer_create_tag(
    buffer,
    "bold",
    "weight", PANGO_WEIGHT_BOLD,
    nullptr
);

gtk_text_buffer_create_tag(
    buffer,
    "italic",
    "style", PANGO_STYLE_ITALIC,
    nullptr
);
```

Formatting should work both on selected text and on text entered subsequently.

Accepted predictions must inherit the formatting active at the cursor.

### **5.3 Undo and redo**

GTK3's standard text buffer does not provide a complete built-in undo stack. Implement a command-based history system recording:

- Text insertion.
- Text deletion.
- Formatting changes.
- Accepted predictions.
- Cursor and selection state.

GTK's user-action signals can be used to group related editing operations.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://docs.gtk.org\&sz=32)

https\://docs.gtk.org+1



Accepting an entire sentence must count as one undoable action.

### **5.4 Ghost-text rendering**

This is one of the most technically important parts of the project.

Predicted text must **not be inserted into the actual document buffer until accepted**.

Otherwise, it can interfere with autosaving, word counts, clipboard operations, undo history and language-model context.

Implement a custom `GtkTextView` subclass:

```
class PredictiveTextView {
    GtkTextView* view;
    std::string ghostText;
    uint64_t predictionRevision;

public:
    void showSuggestion(const std::string& text);
    void clearSuggestion();

    void acceptSuggestion();
    void acceptNextWord();
    void acceptNextSentence();

    void drawGhostText(cairo_t* cr);
};
```

Use Pango to calculate the suggestion's text layout and Cairo to render it at the cursor position.

For the first release:

- Support inline suggestions where sufficient horizontal space exists.
- Support completion at the end of a paragraph.
- Use a cursor-anchored popover when the suggestion cannot fit without overlapping existing text.
- Clear or reposition suggestions when scrolling or resizing.
- Disable suggestions while text is selected or an input method is composing characters.

True multi-line ghost text that participates in paragraph reflow is substantially harder with `GtkTextView`. It should be developed only after the basic prediction interaction is stable.

## **6. Prediction engine architecture**

The prediction system will consist of three complementary engines.

**Engine A — Lexical prediction**

Trie + vocabulary frequency

She stared at him in astonishment

Target latency: under 5 ms

**Engine B — Statistical prediction**

Interpolated n-gram language model

Out of the corner of her eye

Target latency: under 25 ms

**Engine C — Neural prediction**

Quantised local language model

She recognised his handwriting. Even after all these years, there was no mistaking it.

Asynchronous, hardware-dependent latency

All three engines should implement a common interface.

```
struct PredictionRequest {
    std::string documentId;
    std::string textBeforeCursor;
    std::string textAfterCursor;
    std::string currentWordPrefix;

    uint64_t documentRevision;
    uint64_t requestId;

    size_t maxWords;
};

struct PredictionCandidate {
    std::string continuation;
    std::string engineId;

    double rawScore;
    double calibratedConfidence;

    size_t estimatedKeystrokeSavings;

    uint64_t documentRevision;
    uint64_t requestId;
};

class IPredictionEngine {
public:
    virtual ~IPredictionEngine() = default;

    virtual std::vector<PredictionCandidate>
    predict(const PredictionRequest& request) = 0;

    virtual void cancel(uint64_t requestId) = 0;
};
```

## **7. Engine A: Advanced word completion**

### **7.1 Vocabulary sources**

The lexical engine will combine four dictionaries.

| Dictionary          | Contents                                                            |
| ------------------- | ------------------------------------------------------------------- |
| General English     | Common English words                                                |
| Fiction vocabulary  | Words frequently used in novels and short stories                   |
| Personal dictionary | Words learned from the user's writing                               |
| Document dictionary | Character names, places and terminology from the current manuscript |

The general and fiction dictionaries should be read-only, compressed tries. Personal and document dictionaries should remain mutable.

MARISA-trie is suitable for the static vocabulary because it provides compressed storage and predictive prefix searches.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+1



### **7.2 Word prediction algorithm**

```
1. Extract the partially typed word.
2. Normalise the word for lookup.
3. Search the static vocabulary trie.
4. Search the personal vocabulary.
5. Search the current document vocabulary.
6. Merge and deduplicate candidates.
7. Evaluate contextual probability.
8. Rank the candidates.
9. Restore appropriate capitalisation.
10. Return the best completion.
```

Example:

Input:

```
She found the situation increasingly uncom
```

Candidates:

| Candidate      | Illustrative relevance |
| -------------- | ---------------------- |
| uncomfortable  | 94%                    |
| uncomplicated  | 38%                    |
| uncommon       | 27%                    |
| uncompromising | 12%                    |

These scores are illustrative, not measured model probabilities.

The engine displays only the missing suffix:

```
fortable
```

### **7.3 Candidate ranking**

An initial ranking formula could be:

\\[ S(w)=0.40C(w)+0.20F(w)+0.15P(w) +0.15D(w)+0.10K(w) \\]

Where:

- \\(C\\): contextual language-model score.
- \\(F\\): general word frequency.
- \\(P\\): personal usage frequency.
- \\(D\\): current document frequency.
- \\(K\\): estimated keystroke savings.

All features must first be normalised or calibrated to comparable ranges. The initial weights are heuristic and should be tuned using benchmark data.

### **7.4 Additional lexical features**

Support case-sensitive proper nouns, contractions, hyphenated words, British and American spelling, possessives and Unicode punctuation.

If the user types a character name such as *Evangeline* several times, it should quickly become a high-priority candidate when they type `Eva`.

## **8. Engine B: N-gram phrase prediction**

### **8.1 Model architecture**

Use a pruned, smoothed 5-gram language model trained primarily on fictional prose.

Unlike an ordinary dictionary, the model can predict words that commonly occur together.

\\[ P(w_n\mid w\_{n-4},w\_{n-3},w\_{n-2},w\_{n-1}) \\]

For example:

| Existing text       | Possible completion |
| ------------------- | ------------------- |
| In the middle       | of the night        |
| She could not help  | but smile           |
| For a brief         | moment              |
| He took a deep      | breath              |
| There was something | about him           |
| Without saying a    | word                |
| As far as she       | could remember      |

KenLM provides efficient state-based language-model scoring and compressed binary models.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+2



### **8.2 Training corpus**

Start with a carefully selected fiction corpus rather than indiscriminately training on Wikipedia or web pages.

| Corpus category                                      | Proposed proportion |
| ---------------------------------------------------- | ------------------- |
| Novels and short stories                             | 60%                 |
| Contemporary general English with suitable licensing | 20%                 |
| Dialogue-heavy fiction                               | 15%                 |
| Essays and other prose                               | 5%                  |

Potential sources include Project Gutenberg, Standard Ebooks, appropriately licensed fiction datasets and user-provided manuscripts.

Project Gutenberg texts require copyright and jurisdiction checks, particularly when the application is distributed internationally.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.gutenberg.org\&sz=32)

Project Gutenberg+1



### **8.3 Corpus preparation pipeline**

Source books and licensed text

Remove metadata and boilerplate

Normalise Unicode and punctuation

Segment sentences and tokenise

Split by book into train, validation and test sets

Train and prune 3-gram / 5-gram models

Build binary models and candidate index

Prepare approximately 5–10 million tokens for the initial experimental model, followed by larger corpus versions if benchmarks justify them.

Do not lowercase the entire corpus without preserving case information. Capitalisation, dialogue punctuation and sentence boundaries are significant for fiction.

### **8.4 Model training**

Illustrative KenLM training commands:

```
lmplz \
    -o 5 \
    --prune 0 0 1 1 2 \
    < corpus.txt \
    > fiction-5gram.arpa
```

Convert the model into an efficient binary representation:

```
build_binary trie \
    fiction-5gram.arpa \
    fiction-5gram.binary
```

Compare 3-gram and 5-gram models before choosing the default. The larger model may not justify its increased storage and memory requirements.

### **8.5 Next-word candidate generation**

KenLM primarily scores supplied sequences. It is not, by itself, an efficient general-purpose next-word suggestion generator.

Build a separate candidate index mapping contexts to commonly observed continuations.

```
"she looked at" →
    him
    her
    the
    herself

"she looked at him" →
    and
    with
    for
    in
```

At runtime:

1. Retrieve the top continuations for the longest matching context.
2. Back off to shorter contexts when necessary.
3. Merge candidates from personal and document-level models.
4. Score them using KenLM.
5. Expand promising sequences using beam search.
6. Stop at a configured length or sentence boundary.

### **8.6 Phrase generation using beam search**

Start with:

```
Beam width:         5
Maximum expansion:  6 words
Candidates/step:    20
```

Illustrative search:

Use length-normalised log probabilities to compare phrases of different lengths. Penalise repetition and unnecessarily long suggestions.

## **9. Engine C: Neural sentence completion**

This will be the most important component for genuinely reducing the amount of typing involved in story writing.

The statistical model handles predictable language, while the neural model can generate continuations based on meaning, atmosphere, dialogue and narrative context.

### **9.1 Local inference architecture**

Use `llama.cpp` initially as a separate local inference process.

**StoryFlow Editor**

Asynchronous completion request

**Local HTTP interface**

127.0.0.1:8080

**llama.cpp server**

Inference · Prompt cache · Token generation

**Quantised GGUF model**

CPU / optional GPU acceleration

Advantages of using a separate process include crash isolation, easier model replacement, independent resource management and simplified application development.

Embed llama.cpp directly only if later profiling demonstrates a meaningful advantage.

### **9.2 Model selection**

Benchmark small models before committing to a particular one.

| Candidate         | Parameters | Role                          |
| ----------------- | ---------- | ----------------------------- |
| Qwen2.5-0.5B Base | 0.5B       | Lightweight baseline          |
| Qwen2.5-1.5B Base | 1.5B       | Initial preferred candidate   |
| SmolLM2           | 1.7B       | Alternative compact model     |
| Qwen2.5-3B Base   | 3B         | Higher-quality candidate      |
| Mistral 7B Base   | 7B         | Optional higher-resource mode |

These are starting candidates, not a claim that they are the latest or best-performing models.

[Qwen2.5-1.5B Base](https://huggingface.co/Qwen/Qwen2.5-1.5B?utm_source=chatgpt.com) is a reasonable first experimental model and is available under Apache 2.0.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://huggingface.co\&sz=32)

Hugging Face+1



### **Why use a base model rather than a chatbot model?**

A base language model is trained to continue existing text.

Input:

```
Margaret looked towards the door. She had
not expected to see him again, especially
after
```

Desired output:

```
the way they had parted that evening.
```

An instruction-tuned chatbot model may need additional prompting to avoid responses such as “Here is a possible continuation.”

Base models should therefore be the initial focus, although both model types should be evaluated.

### **9.3 Model launch**

Example local server configuration:

```
llama-server \
    -m models/fiction-model.Q4_K_M.gguf \
    -c 4096 \
    -t 4 \
    --host 127.0.0.1 \
    --port 8080
```

Thread counts, context length and quantisation should be configurable.

### **9.4 Completion request**

```
{
    "prompt": "She recognised the handwriting immediately.",
    "n_predict": 40,
    "temperature": 0.65,
    "top_p": 0.9,
    "repeat_penalty": 1.08,
    "cache_prompt": true,
    "stream": true,
    "stop": ["\n\n"]
}
```

The llama.cpp server supports streaming completion, generation limits and prompt-cache reuse.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+1



These generation parameters are provisional defaults.

### **9.5 Prediction modes**

Configurable prediction modes

Word

Phrase

Sentence

Paragraph

She immediately recognised the familiar handwriting, though she had not seen it for twenty years.

Engine

Local language model

Maximum length

Up to one sentence

Scheduling delay

250–400 ms

Illustrative settings and completions. Scheduling delay excludes model inference time.

Paragraph generation should not be enabled automatically by default. The focus is assisting writing rather than generating entire stories autonomously.

### **9.6 Sentence boundary detection**

Do not simply terminate generation at every full stop. That would produce errors with abbreviations, initials, dialogue and ellipses.

Use a sentence-boundary detector combined with:

- Maximum token count.
- Maximum word count.
- Paragraph boundaries.
- Generation time budget.
- End-of-sequence tokens.

For mid-sentence editing where text already follows the cursor, suppress ordinary neural continuation initially. Introduce fill-in-the-middle prediction later using a model explicitly compatible with it.

## **10. Prediction coordinator and scheduling**

Running an LLM on every keystroke would be wasteful and would produce constant flickering suggestions.

Instead, use a progressive prediction pipeline.

Prediction scheduling after a keystrokeIllustrative timing targets, not measured results.

### **Scheduling logic**

```
def on_text_changed():    revision = increment_document_revision()    cancel_obsolete_requests()    update_current_context()    show_fast_lexical_prediction()    schedule_ngram_prediction(revision)    debounce_neural_prediction(revision)
```

When a neural result arrives:

```
def on_prediction_received(result):    if result.revision != current_revision:        return    if result.request_id != active_request_id:        return    if cursor_has_moved():        return    if editor_has_selection():        return    if is_better_and_stable(result):        display_suggestion(result)
```

### **Essential behaviour**

If a suggestion says:

```
the door slowly
```

and the user manually types `the `, the remaining suggestion should become:

```
door slowly
```

The application should retain compatible suggestions rather than immediately regenerate them.

If the user types something incompatible, dismiss the suggestion and request a new one.

### **Threading model**

| Thread                | Responsibility                         |
| --------------------- | -------------------------------------- |
| GTK main thread       | Editor, events, rendering              |
| Statistical worker    | Trie and n-gram requests               |
| Neural request worker | HTTP streaming and response processing |
| Database worker       | Vocabulary updates                     |
| Storage worker        | Autosaving and backup                  |

GTK widgets should only be accessed from the GTK main thread. Workers should receive immutable text snapshots and return results through the GLib main context.

The coordinator should limit itself to one active neural request and one latest pending request to avoid accumulating obsolete predictions.

## **11. Intelligent ranking and prediction stability**

Different engines produce different kinds of scores. A trie frequency score is not directly comparable to a neural token probability.

Therefore, the ranking system must calibrate scores and prioritise expected usefulness.

A useful objective is:

\\[ U(s)=p_a(s)\cdot K(s)-C_i(s)-C_d(s) \\]

Where:

- \\(p_a(s)\\) is the estimated probability that the suggestion will be accepted.
- \\(K(s)\\) is the number of keystrokes it could save.
- \\(C_i(s)\\) is the interaction cost of accepting it.
- \\(C_d(s)\\) is the estimated distraction cost.

This is more appropriate than choosing the longest or most probable continuation regardless of usefulness.

### **Example ranking**

Context: She could not help...

but smile.

N-gram · 92/100

but laugh.

N-gram · 76/100

but wonder whether he remembered her.

Neural · 65/100

herself.

Lexical · 31/100

Illustrative calibrated utility scores.

The application should also use a stability threshold: do not replace an already visible suggestion merely because a slightly better candidate appears.

## **12. Personalised learning system**

The application should become progressively better as it is used, without requiring neural-model retraining.

### **12.1 SQLite database**

```
CREATE TABLE personal_words (
    word TEXT PRIMARY KEY,
    usage_count INTEGER DEFAULT 0,
    accepted_count INTEGER DEFAULT 0,
    last_used INTEGER
);

CREATE TABLE personal_ngrams (
    context TEXT NOT NULL,
    continuation TEXT NOT NULL,
    usage_count INTEGER DEFAULT 0,
    PRIMARY KEY (context, continuation)
);

CREATE TABLE prediction_events (
    id INTEGER PRIMARY KEY,
    engine TEXT,
    offered_chars INTEGER,
    accepted_chars INTEGER,
    accepted INTEGER,
    latency_ms REAL,
    timestamp INTEGER
);
```

The event table should not store full manuscript passages by default.

### **12.2 Learning from writing**

Update the personal model from:

- Manually completed words.
- Manually written phrases.
- Accepted suggestions.
- Imported manuscripts, with explicit permission.
- Frequently repeated character names and locations.

Do not learn from text that has merely been suggested but never accepted.

Accepted AI-generated text should initially receive less learning weight than independently authored text, to reduce self-reinforcing stylistic repetition.

### **12.3 Adaptive language model**

Combine the general, fiction, personal and document-level models.

\\[ P(w\mid h)= \lambda_gP_g(w\mid h)+ \lambda_fP_f(w\mid h)+ \lambda_pP_p(w\mid h)+ \lambda_dP_d(w\mid h) \\]

Initial weights could be:

Experimental personalisation settings

General English

20%

Fiction corpus

45%

Personal writing

20%

Current manuscript

15%

Restore defaults

Weights should adapt as more personal writing becomes available. A newly installed application should rely heavily on its pretrained models.

### **12.4 Writing profiles**

Allow the user to maintain separate personalisation profiles:

| Profile          | Purpose                           |
| ---------------- | --------------------------------- |
| Fiction          | Narrative prose                   |
| Literary fiction | Descriptive and stylistic writing |
| Academic         | Research and scholarly prose      |
| Personal         | General writing                   |

Each profile should have independent vocabulary statistics and phrase frequencies.

## **13. Story context and narrative memory**

A generic sentence-completion engine may produce grammatically correct but narratively inappropriate suggestions.

For example, it might suggest a character doing something inconsistent with events established earlier in the manuscript.

To address this, implement a story-aware context manager.

### **13.1 Context hierarchy**

**Level 1 — Immediate context**

Current sentence and preceding paragraph

**Level 2 — Scene context**

Recent paragraphs, dialogue and events

**Level 3 — Chapter context**

Chapter summary and important narrative details

**Level 4 — Story memory**

Character information, locations, timeline and previous events

### **13.2 Character database**

```
{
    "characters": [
        {
            "id": "margaret",
            "name": "Margaret",
            "aliases": ["Maggie"],
            "age": 42,
            "occupation": "Teacher",
            "personality": [
                "reserved",
                "observant"
            ],
            "relationships": {
                "edward": "estranged husband"
            },
            "notes": "Has not seen Edward for twenty years."
        }
    ]
}
```

### **13.3 Context retrieval**

For each neural prediction, retrieve only the most relevant information.

The proposed initial context budget:

| Context component        | Tokens    |
| ------------------------ | --------- |
| Relevant story notes     | 200–400   |
| Chapter or scene summary | 200–400   |
| Recent manuscript text   | 800–1,500 |
| Current paragraph        | 100–300   |
| Reserved output space    | 64–128    |

These are approximate allocations. The actual context builder must respect the tokenizer and model's context window.

For base models, metadata conditioning should be evaluated carefully because adding structured notes to the prompt can disrupt natural prose continuation.

### **13.4 Retrieval-augmented prediction**

Start with simple retrieval rather than a vector database.

SQLite FTS5 can index chapters, notes and character descriptions. Retrieve passages based on character names, location names, important terms and scene relevance.

Later, semantic embeddings could retrieve relevant story details that do not share exact keywords with the current passage.

## **14. Keyboard interaction design**

The acceptance mechanism should be fast enough that using it becomes almost unconscious.

| Shortcut             | Action                           |
| -------------------- | -------------------------------- |
| Tab                  | Accept entire visible suggestion |
| Alt + Right          | Accept next word                 |
| Ctrl + Right         | Normal word navigation           |
| Alt + Down           | Next alternative                 |
| Alt + Up             | Previous alternative             |
| Esc                  | Dismiss suggestion               |
| Ctrl + Space         | Request prediction manually      |
| Ctrl + Shift + Space | Request a longer continuation    |
| Ctrl + B             | Bold                             |
| Ctrl + I             | Italic                           |
| Ctrl + U             | Underline                        |
| Ctrl + Z             | Undo                             |
| Ctrl + Shift + Z     | Redo                             |
| F11                  | Distraction-free fullscreen      |

When there is no active suggestion, Tab should perform its normal editing function.

All prediction shortcuts should be configurable.

### **Incremental acceptance**

Suppose the editor suggests:

She knew that something was terribly wrong.

Accept wordAccept allReset

Interactive demonstration of progressive acceptance.

The author may accept two words and then finish the sentence independently. This is particularly useful when a prediction is partly correct.

## **15. Document storage and file formats**

Avoid making the user dependent on a proprietary document format.

### **Proposed project structure**

```
MyNovel/
├── project.json
├── chapters/
│   ├── 001.html
│   ├── 002.html
│   └── 003.html
├── notes/
│   ├── characters.json
│   ├── locations.json
│   └── timeline.json
├── summaries/
│   └── chapters.json
└── backups/
```

The canonical chapter format should be a restricted subset of semantic HTML.

```
<h1>Chapter One</h1>

<p>
    The rain had not stopped since morning.
    <em>Perhaps he would never return.</em>
</p>

<p>
    She placed the <strong>letter</strong>
    on the desk.
</p>
```

HTML preserves the required formatting, is human-readable and can be opened independently in other applications.

### **Export formats**

| Format     | Priority       |
| ---------- | -------------- |
| HTML       | MVP            |
| Plain text | MVP            |
| Markdown   | MVP            |
| DOCX       | Second release |
| EPUB       | Second release |
| PDF        | Later release  |

Pandoc can handle several export conversions, avoiding the need to implement complex document standards independently.

### **Autosave and recovery**

Implement periodic autosave, atomic file replacement and timestamped backups. Keep recovery files until the canonical document has been saved successfully.

Under no circumstances should an unaccepted prediction appear in a saved document or exported file.

## **16. Repository structure**

```
storyflow/
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── application.cpp
│   │   └── preferences.cpp
│   │
│   ├── editor/
│   │   ├── editor.cpp
│   │   ├── text_view.cpp
│   │   ├── formatting.cpp
│   │   ├── undo_manager.cpp
│   │   └── ghost_text.cpp
│   │
│   ├── prediction/
│   │   ├── engine.hpp
│   │   ├── request.hpp
│   │   ├── candidate.hpp
│   │   ├── coordinator.cpp
│   │   ├── trie_engine.cpp
│   │   ├── ngram_engine.cpp
│   │   ├── neural_engine.cpp
│   │   ├── beam_search.cpp
│   │   └── ranker.cpp
│   │
│   ├── learning/
│   │   ├── database.cpp
│   │   ├── vocabulary.cpp
│   │   └── personal_ngrams.cpp
│   │
│   ├── context/
│   │   ├── context_manager.cpp
│   │   ├── tokenizer.cpp
│   │   ├── story_memory.cpp
│   │   └── retrieval.cpp
│   │
│   ├── document/
│   │   ├── document.cpp
│   │   ├── html_serializer.cpp
│   │   ├── autosave.cpp
│   │   └── recovery.cpp
│   │
│   └── ui/
│       ├── main_window.cpp
│       ├── settings_dialog.cpp
│       └── chapter_sidebar.cpp
│
├── resources/
│   ├── icons/
│   ├── themes/
│   └── ui/
│
├── models/
│   └── manifest.json
│
├── tools/
│   ├── prepare_corpus.py
│   ├── build_vocabulary.py
│   ├── train_ngram.sh
│   ├── build_candidate_index.py
│   └── benchmark.py
│
├── tests/
│   ├── test_editor.cpp
│   ├── test_trie.cpp
│   ├── test_ngram.cpp
│   ├── test_coordinator.cpp
│   ├── test_serialization.cpp
│   └── test_learning.cpp
│
├── packaging/
│   ├── debian/
│   ├── appimage/
│   └── storyflow.desktop
│
└── docs/
    ├── architecture.md
    ├── model-training.md
    ├── prediction-protocol.md
    └── development-roadmap.md
```

## **17. Development roadmap**

The following work packages can serve directly as milestones and GitHub issues.

01

**Editor foundation**

v0.1

GTK3 application, formatting, document operations

02

**Predictive interaction**

v0.2

Ghost text, acceptance, cancellation, undo

03

**Lexical engine**

v0.3

Trie, vocabulary, prefix ranking

04

**Statistical engine**

v0.4

Corpus preparation, KenLM, phrase prediction

05

**Neural completion**

v0.5

Local inference, streaming, sentence suggestions

06

**Personalisation**

v0.6

SQLite learning and adaptive ranking

07

**Story awareness**

v0.7

Chapters, characters, context retrieval

08

**Optimisation and release**

v1.0

Benchmarks, packaging, reliability

### **Phase 1 — Editor foundation (v0.1)**

Implement the editor independently of the prediction system.

Implementation checklist

0/10

Initialise CMake and C++ project

Create GtkApplication and main window

Implement GtkTextView editor

Implement bold and italic formatting

Implement file open, save and save as

Implement undo and redo

Implement HTML serialisation

Implement autosave and recovery

Implement word count and preferences

Add editor unit tests and Linux CI

**Completion criterion:** A reliable standalone rich-text editor capable of writing and saving short stories.

### **Phase 2 — Prediction interaction (v0.2)**

Implement a mock prediction engine returning deterministic suggestions. This avoids involving machine-learning complexity while developing the interaction.

Implementation checklist

0/10

Define prediction request and result interfaces

Implement mock prediction engine

Implement cursor-anchored ghost text

Implement Tab acceptance

Implement word-by-word acceptance

Implement Esc dismissal

Implement alternative cycling

Handle cursor movement and selection

Implement revision-based invalidation

Verify suggestions never enter saved documents

**Completion criterion:** Copilot-style text acceptance works reliably even though the suggestions are not yet intelligent.

### **Phase 3 — Lexical engine (v0.3)**

Implement MARISA-trie integration, vocabulary preparation, case-sensitive prefix matching, word frequency ranking and document-specific vocabulary.

**Completion criterion:** Partially typed words are completed with negligible perceptible delay.

### **Phase 4 — Statistical engine (v0.4)**

Implement corpus preparation, comparative 3-gram and 5-gram training, binary model loading, next-word candidate indexing, contextual ranking and beam-search phrase prediction.

**Completion criterion:** The application predicts familiar phrases and contextually relevant words without a neural model.

### **Phase 5 — Neural sentence prediction (v0.5)**

Implementation checklist

0/11

Integrate llama.cpp server

Implement model loading and configuration

Implement asynchronous HTTP client

Implement streamed completion parsing

Implement context extraction

Implement debounce and cancellation

Implement sentence boundary detection

Implement prompt caching

Benchmark compact base models

Integrate neural and statistical ranking

Implement graceful fallback when inference fails

**Completion criterion:** The author can accept contextually generated sentences and continue writing without UI freezes.

This is the first release that delivers the complete core experience you described.

### **Phase 6 — Personalisation (v0.6)**

Implement SQLite-backed vocabulary, personal phrase counts, acceptance statistics, manuscript import, adaptive ranking and resettable writing profiles.

**Completion criterion:** Prediction quality improves measurably when the system is exposed to the author's own writing.

### **Phase 7 — Story awareness (v0.7)**

Implement multi-chapter projects, character and location notes, chapter summaries, FTS5 retrieval, scene-aware context construction and optional semantic retrieval.

**Completion criterion:** Sentence completion can incorporate established narrative details.

### **Phase 8 — Stable release (v1.0)**

Optimise startup, memory consumption and latency. Complete accessibility testing, X11 and Wayland testing, model installation, package creation, export support and documentation.

## **18. Hardware and resource management**

The application should offer selectable performance profiles.

**Lightweight**

# **4 GB RAM**

Trie and n-gram only. No neural model loaded.

**Balanced**

# **8 GB RAM**

Small quantised neural model with statistical fallback.

**Advanced**

# **16 GB RAM**

Larger model and extended story context.

**Accelerated**

# **GPU**

Optional Vulkan, CUDA or other supported acceleration.

Actual model suitability depends on CPU performance, available memory, context size and competing applications.

### **Resource optimisations**

- Load the neural model lazily.
- Keep static dictionaries memory-mapped where appropriate.
- Reuse the language model's context cache.
- Cancel outdated neural requests.
- Limit the number of inference threads.
- Perform database writes in batches.
- Unload the neural model during extended inactivity if configured.
- Keep the statistical engine available when neural prediction is disabled.

## **19. Prediction quality evaluation**

This should be treated as a small computational linguistics project as well as a software development project.

### **19.1 Benchmark systems**

Compare the following configurations using identical held-out fiction passages.

| Experiment | Configuration                       |
| ---------- | ----------------------------------- |
| A          | Frequency-based trie                |
| B          | Trie + trigram                      |
| C          | Trie + 5-gram                       |
| D          | Trie + 5-gram + personal vocabulary |
| E          | Neural model only                   |
| F          | Hybrid statistical and neural model |
| G          | Hybrid model + story context        |

### **19.2 Evaluation metrics**

| Metric                      | Meaning                                      |
| --------------------------- | -------------------------------------------- |
| Top-1 accuracy              | Correct intended continuation ranked first   |
| Top-5 accuracy              | Correct continuation among five alternatives |
| Mean reciprocal rank        | Quality of candidate ranking                 |
| Keystroke savings rate      | Reduction in simulated typing                |
| Acceptance rate             | Suggestions accepted by actual users         |
| Partial acceptance rate     | Suggestions accepted in part                 |
| Time to first suggestion    | Responsiveness                               |
| Time to complete suggestion | Neural inference performance                 |
| Suggestion stability        | Frequency of unnecessary changes             |
| Effective writing speed     | Net improvement in real writing              |

### **19.3 Keystroke savings calculator**

\\[ KSR=\frac{K\_{\text{manual}}-K\_{\text{assisted}}} {K\_{\text{manual}}}\times100 \\]

Simulated typing reductionOriginal number of keystrokes

Keystroke savings rate

**35%**



0%

25%

50%

Typing speed (keystrokes/min)

**220**



Keystrokes saved

# **3,500**

Remaining keystrokes

# **6,500**

Manual typing

# **45.5 min**

Gross motor time saved

# **15.9 min**

Excludes time spent thinking, reading suggestions, correcting predictions and making decisions. Actual writing-time savings may be substantially different.

### **19.4 Initial performance targets**

| Metric                                           | Engineering target                   |
| ------------------------------------------------ | ------------------------------------ |
| Trie prediction p95 latency                      | Below 5 ms                           |
| Statistical prediction p95 latency               | Below 25 ms                          |
| GTK main-thread frame budget                     | Approximately 16 ms                  |
| Neural first suggestion                          | Below 1 second on reference hardware |
| Statistical engine simulated keystroke savings   | 25–40%                               |
| Hybrid engine simulated keystroke savings        | 35–50%                               |
| Unaccepted suggestions saved to documents        | Zero                                 |
| Stale predictions accepted after cursor movement | Zero                                 |

These are proposed acceptance targets, not demonstrated results. Define the reference hardware before using them as formal release criteria.

For real-world evaluation, conduct free-writing trials as well as simulated transcription. The two tasks are fundamentally different: a story writer may accept an unexpected but useful continuation that does not match any predetermined reference text.

## **20. Testing strategy**

### **Unit tests**

Test prefix matching, ranking, Unicode handling, formatting, serialisation, phrase generation, personal frequency updates and sentence boundary detection.

### **Integration tests**

Test editor interactions involving multiple components, particularly:

| Scenario                               | Expected behaviour                        |
| -------------------------------------- | ----------------------------------------- |
| User continues typing during inference | Obsolete result discarded                 |
| User moves cursor                      | Suggestion invalidated                    |
| User accepts sentence                  | One undo action                           |
| User presses Esc                       | Suggestion disappears                     |
| Document switches                      | Previous document's predictions discarded |
| Neural server crashes                  | Editor remains usable                     |
| Autosave occurs during prediction      | Only committed text saved                 |
| User types in italic mode              | Accepted text inherits italics            |
| Suggestion extends beyond line width   | Safe overlay or popover                   |
| User pastes a large passage            | Editor remains responsive                 |
| Input method is active                 | Predictions do not disrupt composition    |

### **Regression tests**

Create a collection of fixed fictional passages and recorded prediction requests. Run these whenever tokenisation, ranking, scheduling or model integration changes.

Use deterministic mock predictions in CI. Neural-model benchmarks should run separately because model downloads and hardware requirements would otherwise make ordinary builds unnecessarily expensive.

## **21. Major technical risks**

| Risk                                        | Severity    | Mitigation                                         |
| ------------------------------------------- | ----------- | -------------------------------------------------- |
| Ghost-text rendering and line wrapping      | High        | Prototype early; use popover fallback              |
| Slow CPU-based neural inference             | High        | Compact models, caching, statistical fallback      |
| Stale asynchronous suggestions              | High        | Revision IDs and strict invalidation               |
| Formatting and undo corruption              | High        | Independent edit transactions                      |
| Generic or repetitive prose                 | Medium–high | Fiction corpus, adaptive ranking, model evaluation |
| Inconsistent narrative facts                | Medium      | Story context retrieval                            |
| Large n-gram model size                     | Medium      | Pruning and comparative benchmarking               |
| Personal model reinforcing poor predictions | Medium      | Weight manually authored text more highly          |
| Copyright restrictions on training data     | Medium      | Corpus provenance and licence audit                |
| Suggestion distraction                      | Medium      | Confidence thresholds and stable suggestions       |

## **22. Build environment**

For Debian, Ubuntu and related XFCE distributions, the initial development dependencies could be installed with:

```
sudo apt install \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    libgtk-3-dev \
    libmarisa-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    libicu-dev \
    nlohmann-json3-dev
```

Build the application with:

```
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build

ctest --test-dir build

./build/storyflow
```

KenLM and llama.cpp should be pinned to tested versions. Package names and additional build dependencies will vary across distributions.

The editor must compile and operate even when optional prediction backends are not installed.

## **23. Proposed first pull request**

The first implementation should deliberately avoid integrating real language models.

**PR #1 — GTK editor foundation and mock predictive text**

Scope:

- Create C++ and CMake project.
- Create native GTK3 application.
- Implement basic rich-text editing.
- Implement bold and italic.
- Implement HTML opening and saving.
- Define prediction engine interface.
- Implement deterministic mock predictor.
- Display faint suggestion text at the cursor.
- Accept suggestions with Tab.
- Accept individual words with Alt + Right.
- Dismiss suggestions with Esc.
- Implement undo for accepted suggestions.
- Add automated tests and Linux CI.

Out of scope:

- Actual neural inference.
- KenLM integration.
- Training corpora.
- Character database.
- Multi-chapter management.
- Advanced export.
- Cloud services.

This first pull request will establish whether the most important part of the application — the continuous writing and prediction experience — works correctly.

## **24. Future advanced features**

After the core application is stable, additional research and development could include:

| Feature                     | Implementation                                            |
| --------------------------- | --------------------------------------------------------- |
| Neural speculative decoding | Use a smaller model to accelerate larger-model generation |
| Style-specific prediction   | Lightweight LoRA adapters                                 |
| Semantic story memory       | Local embedding model and vector retrieval                |
| Fill-in-the-middle          | Prediction when editing existing sentences                |
| Dialogue prediction         | Character-conditioned generation                          |
| Adaptive generation length  | Learn the author's preferred suggestion length            |
| Grammar-aware reranking     | Penalise syntactically implausible continuations          |
| Multilingual prediction     | Separate models for English, Hindi and other languages    |
| Personal model training     | Optional fine-tuning on user-owned manuscripts            |
| System-wide prediction      | Reuse the prediction library through IBus or Fcitx5       |

## **25. Final product specification**

# **StoryFlow**

Native, offline, predictive writing editor for Linux

**Rich-text writing**

Bold, italic, headings

**Instant completion**

Trie and n-gram

**Sentence prediction**

Local neural model

**Personal learning**

Adaptive vocabulary

**Story awareness**

Characters and context

**Private by design**

Offline operation

**Fast interaction**

Progressive acceptance

**Open formats**

HTML, Markdown, DOCX

### **Recommended development approach**

Although the full architecture incorporates a trie, n-gram models and neural inference, I would prioritise two things above everything else:

1. **A reliable, natural ghost-text editing experience.**
2. **Fast, high-quality sentence completion using a small local language model.**

The n-gram and trie engines will make short completions faster and reduce unnecessary neural inference. However, sentence-level prediction is what will differentiate this application from LibreOffice's traditional word completion.

The proposed GTK3 stack, English-first corpus, offline-first architecture and project name are design decisions I have made for this plan. Your actual hardware specifications are not yet known, and the prediction quality and latency targets require benchmarking.

The ultimate goal is an editor that feels like a natural extension of your thinking: you begin a sentence, the application anticipates its continuation, and you accept only as much as you intended to write.
