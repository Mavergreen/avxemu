# Release notes

`release.yml` builds each release's notes with shipyard's `release-notes.sh`. A file here
named `<version>.md` adds prose to that release, verbatim, right after the generated title.
Do not start one with a heading: the generator writes the title.
