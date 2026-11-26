"""Generate the bundled phrase table from NLTK's Gutenberg corpus ZIP."""

import argparse
from collections import Counter, defaultdict
import re
from zipfile import ZipFile


def train(archive):
    counts = defaultdict(Counter)
    with ZipFile(archive) as corpus:
        for name in corpus.namelist():
            if not re.search(r"/(austen-|bryant-|burgess-|carroll-|chesterton-|edgeworth-|melville-)", name):
                continue
            text = corpus.read(name).decode("latin-1").lower()
            context = []
            for token in re.findall(r"[a-z]+(?:'[a-z]+)*|[^a-z \t\r]", text):
                word = token.isalpha()
                for length in range(1, len(context) + 1):
                    counts[tuple(context[-length:])][token if word else ""] += 1
                context = (context + [token])[-2:] if word else []

    for context, outcomes in sorted(counts.items()):
        word, count = min(outcomes.items(), key=lambda item: (-item[1], item[0]))
        minimum, confidence = (12, 30) if len(context) == 1 else (6, 40)
        if word and count >= minimum and 100 * count >= confidence * sum(outcomes.values()):
            print(" ".join(context), word, sep="\t")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    train(parser.parse_args().archive)
