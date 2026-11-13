# Existing detector artifacts: local editor integration

Codex, 2026-11-13. This review extends the existing local-experiment use to an
isolated owner-side editor integration test, not distribution or live user input.
The owner requested connecting the checked dictionary packages to the detector
and test editor, then confirmed "делаем". This is a new, scoped integration action;
it does not reuse the completed raw dictionary smoke's one-shot permission.

The model is the unchanged experiment002 RSKN artifact (88,290 bytes, SHA-256
in browser-profile.json); stems are the unchanged RSKP pair (1,200,379 bytes).
Metadata names the same exact artifacts, dictionary source hashes, training
manifest and frozen protocol. Training uses experiment001 train words only, not
Tatoeba/Rust Book/user input. No training, threshold change or corpus read is
performed in this integration. Prior operation notices and restrictions remain.

The original artifact-metadata.json and MODIFICATIONS.txt are pinned and retained,
alongside both full upstream notices. The latter explicitly marks the numeric
artifacts as modified derivatives for local experiment only, release_approved=false.
The exact stem lexicon is a dictionary derivative; compression/model training do
not erase source obligations. No blanket MIT claim is made for either artifact.
The applicable primary dictionary terms were read in LANGUAGE-PACKS-REVIEW.md;
English composite SCOWL/Ispell/WordNet/VarCon/UKACD notices remain complete.

Allowed here: hash/size and format verification of the six named raw/derived
inputs, copying them with full notices into ignored build/, inference on fixed
project-authored neutral test cases in a disposable editor, and aggregate results.
Metadata and notice copies are additional explicitly listed inputs, not permission
to scan the artifact directory. No reference-model.bin, corpora, feature/label files,
personal text, network, host profile, seat, GPU, installation or live account.

Distribution remains false. Before shipping, separately review the actual model,
stem, dictionaries, engine/runtime package and all attribution/source obligations.
This integration is a mechanism test, not a new independent accuracy benchmark or
permission for automatic changes in arbitrary real editors. Further personal/live
input trials require an explicit scope and reviewed installation/permissions.
