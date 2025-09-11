#!/bin/bash

# List of all conflicting files to resolve by removal
files=(
    "misc/msvc12/cgame.vcxproj"
    "misc/msvc12/game.vcxproj"
    "misc/msvc12/opengl1.vcxproj"
    "misc/msvc12/opengl2.vcxproj"
    "misc/msvc12/q3_ui.vcxproj"
    "misc/msvc12/quake3.vcxproj"
    "misc/msvc12/quake3.vcxproj.filters"
    "misc/msvc12/ui.vcxproj"
    "misc/msvc142/cgame.vcxproj"
    "misc/msvc142/game.vcxproj"
    "misc/msvc142/opengl1.vcxproj"
    "misc/msvc142/opengl2.vcxproj"
    "misc/msvc142/q3_ui.vcxproj"
    "misc/msvc142/quake3.vcxproj"
    "misc/msvc142/ui.vcxproj"
)

# Loop through the list of files and remove them
for file in "${files[@]}"
do
    # Git command to check out the version from upstream/main (deleted version)
    git rm --cached "$file"
    rm "$file"
done

# Stage the resolved conflicts for commit
git add .

echo "Merge conflicts resolved by discarding local changes. You can now commit the merge."

