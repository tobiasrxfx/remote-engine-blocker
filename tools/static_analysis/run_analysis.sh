#!/bin/bash

TOOLS_DIR=$(pwd)
BUILD_DIR="$TOOLS_DIR/build"
SRC_DIR="../../src/reb_core"

mkdir -p "$BUILD_DIR"

echo "[STEP 1] Generating analysis artifacts..."
cd "$SRC_DIR" || exit
cppcheck --dump *.c

mv -f *.dump "$BUILD_DIR" 2>/dev/null
mv -f *.ctu-info "$BUILD_DIR" 2>/dev/null

echo ""
echo "[STEP 2] Verifying MISRA C compliance..."
cd "$BUILD_DIR" || exit

MISRA_SUPPRESS="17.3,21.6,21.10,8.7,2.5"

for f in *.dump; do
    echo "Analyzing: $f"
    python "/c/Program Files/Cppcheck/addons/misra.py" \
        --rule-text="$TOOLS_DIR/rules.txt" \
        --suppress-rules "$MISRA_SUPPRESS" \
        "$f"
done

cd "$TOOLS_DIR" || exit

echo ""
echo "--- Analysis Complete ---"
read -p "Press enter to continue..."