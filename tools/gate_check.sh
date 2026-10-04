#!/usr/bin/env bash
# ==============================================================================
# tools/gate_check.sh - Quality Gate & Safety Validation Script
# 
# Enforces mandatory rules R1-R22 from 'regras agente.md' (Section 0.7).
# Must be executed before every commit and in CI.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

FAILED=0

log_info() {
    echo -e "${BLUE}[GATE INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[GATE PASS]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[GATE WARN]${NC} $1"
}

log_fail() {
    echo -e "${RED}[GATE FAIL]${NC} $1"
    FAILED=1
}

echo "========================================================================"
echo " Running Quality Gate Check (regras agente.md - Seção 0.7)"
echo " Repository: ${REPO_ROOT}"
echo "========================================================================"

# Determine mode: STAGED files (pre-commit) or ALL tracked files
CHECK_STAGED=1
if [ "${1:-}" = "--all" ]; then
    CHECK_STAGED=0
    log_info "Mode: Checking all tracked repository files."
else
    log_info "Mode: Checking staged files (pre-commit context)."
fi

# Obtain file list to inspect
if [ "$CHECK_STAGED" -eq 1 ]; then
    CHANGED_FILES=$(git diff --cached --name-only --diff-filter=ACMR || true)
    if [ -z "$CHANGED_FILES" ]; then
        log_info "No staged changes detected. Gate check completed successfully."
        exit 0
    fi
fi

# ------------------------------------------------------------------------------
# CHECK 1: Bloqueio de arquivos de jogo, segredos e arquivos gigantes (R4, R5, 0.7 item 1)
# ------------------------------------------------------------------------------
log_info "Check 1: Security, secrets, and protected assets..."

BLOCKED_REGEX='\.(3ds|cci|cia|cxi|3dsx|env)$|(code\.bin|romfs\.bin|aes_keys|seeddb|keys\.txt|\.git-credentials|\.id_rsa)'
MAX_FILE_SIZE_KB=15360 # 15 MB limit

if [ "$CHECK_STAGED" -eq 1 ]; then
    for file in $CHANGED_FILES; do
        if echo "$file" | grep -E -q "$BLOCKED_REGEX"; then
            log_fail "Blocked or sensitive file detected: $file (Violates R5)"
        fi
        if [ -f "$file" ] && [ "$file" != "source.zip" ]; then
            SIZE_KB=$(du -k "$file" | cut -f1)
            if [ "$SIZE_KB" -gt "$MAX_FILE_SIZE_KB" ]; then
                log_fail "File $file exceeds maximum size limit (${SIZE_KB}KB > ${MAX_FILE_SIZE_KB}KB). (Violates R5)"
            fi
        fi
    done
else
    # Fast check across tracked files
    MATCHES=$(git ls-files | grep -E "$BLOCKED_REGEX" || true)
    if [ -n "$MATCHES" ]; then
        log_fail "Blocked files found in repo:\n$MATCHES"
    fi
fi

if [ "$FAILED" -eq 0 ]; then
    log_pass "Check 1 passed: No protected files, keys, or oversized assets staged."
fi

# ------------------------------------------------------------------------------
# CHECK 2: Bloqueio de commit/push direto na branch principal (R1, 0.7 item 2)
# ------------------------------------------------------------------------------
log_info "Check 2: Branch safety (R1, R4)..."

CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "HEAD")

if [ "$CURRENT_BRANCH" = "main" ] || [ "$CURRENT_BRANCH" = "master" ]; then
    log_fail "Direct commits to '$CURRENT_BRANCH' are prohibited! (Violates R1). Must use feature/native-render-* branch."
else
    log_pass "Check 2 passed: Working on safe branch '$CURRENT_BRANCH'."
fi

# ------------------------------------------------------------------------------
# CHECK 3: Verificação de suíte de testes e skips não autorizados (R10, 0.7 item 3)
# ------------------------------------------------------------------------------
log_info "Check 3: Test integrity and skip validation (R10)..."

if [ "$CHECK_STAGED" -eq 1 ]; then
    for file in $CHANGED_FILES; do
        if [[ "$file" =~ (test_.*|Test-.*|\.test\.) ]] && [ -f "$file" ]; then
            if git diff --cached "$file" 2>/dev/null | grep -E '^\+[ ]*(GTEST_SKIP|@skip|@pytest\.mark\.skip|it\.skip)' | grep -iv "reason" > /dev/null; then
                log_fail "Test skipped without explicit justification in $file (Violates R10)."
            fi
        fi
    done
fi
log_pass "Check 3 passed: No unverified test skips detected."

# ------------------------------------------------------------------------------
# CHECK 4: Verificação de gl_FragDepth e Geometry Shader (R17, 0.7 item 4)
# ------------------------------------------------------------------------------
log_info "Check 4: Prohibited shader features (R17: gl_FragDepth / Geometry Shader)..."

DISALLOWED_FOUND=0
if [ "$CHECK_STAGED" -eq 1 ]; then
    for file in $CHANGED_FILES; do
        if [[ "$file" =~ \.(frag|vert|geom|glsl|hlsl|cpp|hpp|c|h)$ ]] && [ -f "$file" ]; then
            if grep -n "gl_FragDepth" "$file" > /dev/null 2>&1; then
                if ! grep -i "gl_FragDepth" DECISIONS.md > /dev/null 2>&1; then
                    log_fail "Found 'gl_FragDepth' in $file without justification in DECISIONS.md (Violates R17)."
                    DISALLOWED_FOUND=1
                else
                    log_warn "Notice: gl_FragDepth found in $file, documented in DECISIONS.md."
                fi
            fi

            if grep -E -n "layout\s*\(.*(triangles|points|lines).*\)\s*in;" "$file" > /dev/null 2>&1 || \
               grep -n "VK_SHADER_STAGE_GEOMETRY_BIT" "$file" > /dev/null 2>&1; then
                if ! grep -i "geometry shader" DECISIONS.md > /dev/null 2>&1; then
                    log_fail "Found Geometry Shader in $file without justification in DECISIONS.md (Violates R17)."
                    DISALLOWED_FOUND=1
                else
                    log_warn "Notice: Geometry shader usage found in $file, documented in DECISIONS.md."
                fi
            fi
        fi
    done
else
    # In --all mode, use fast git grep
    if git grep -n "gl_FragDepth" -- '*.frag' '*.vert' '*.glsl' 2>/dev/null; then
        if ! grep -i "gl_FragDepth" DECISIONS.md > /dev/null 2>&1; then
            log_fail "Found 'gl_FragDepth' without justification in DECISIONS.md (Violates R17)."
            DISALLOWED_FOUND=1
        fi
    fi
fi

if [ "$DISALLOWED_FOUND" -eq 0 ]; then
    log_pass "Check 4 passed: Main render path respects early-Z and tile-friendly rendering (R17)."
fi

# ------------------------------------------------------------------------------
# CHECK 5: Presença e governança de STATUS.md e DECISIONS.md (R20, 0.7 item 6)
# ------------------------------------------------------------------------------
log_info "Check 5: Governance files presence (R20, STATUS.md and DECISIONS.md)..."

if [ ! -f "STATUS.md" ]; then
    log_fail "Mandatory governance file STATUS.md is missing! (Violates R20)."
fi

if [ ! -f "DECISIONS.md" ]; then
    log_fail "Mandatory governance file DECISIONS.md is missing! (Violates R20)."
fi

if [ -f "STATUS.md" ] && [ -f "DECISIONS.md" ]; then
    log_pass "Check 5 passed: STATUS.md and DECISIONS.md are present."
fi

# ------------------------------------------------------------------------------
# CHECK 6: Validação de NATIVE_FALLBACK em STATUS.md (R26, 0.7 item 7)
# ------------------------------------------------------------------------------
log_info "Check 6: NATIVE_FALLBACK registration in STATUS.md (R26)..."

FALLBACK_CHECK_FAILED=0
if [ "$CHECK_STAGED" -eq 1 ]; then
    FALLBACK_FILES=$(git diff --cached --name-only | grep -E '\.(cpp|c|h|hpp)$' || true)
else
    FALLBACK_FILES=$(git ls-files | grep -E '\.(cpp|c|h|hpp)$' || true)
fi

if [ -n "$FALLBACK_FILES" ]; then
    while IFS= read -r f; do
        if [ -f "$f" ]; then
            MATCHES=$(grep -n "NATIVE_FALLBACK(" "$f" || true)
            if [ -n "$MATCHES" ]; then
                while IFS= read -r line; do
                    FALLBACK_ID=$(echo "$line" | awk -F'[(),]' '{print $3}' | tr -d ' "')
                    if [ -z "$FALLBACK_ID" ]; then
                        FALLBACK_ID=$(echo "$line" | awk -F'[()]' '{print $2}' | tr -d ' "')
                    fi
                    if [ -n "$FALLBACK_ID" ]; then
                        if ! grep -q "$FALLBACK_ID" STATUS.md; then
                            log_fail "NATIVE_FALLBACK with id '$FALLBACK_ID' in $f is not registered in STATUS.md (Violates R26 / 0.7 item 7)."
                            FALLBACK_CHECK_FAILED=1
                        fi
                    fi
                done <<< "$MATCHES"
            fi
        fi
    done <<< "$FALLBACK_FILES"
fi

if [ "$FALLBACK_CHECK_FAILED" -eq 0 ]; then
    log_pass "Check 6 passed: All NATIVE_FALLBACK instances in code are documented in STATUS.md."
fi

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
echo "========================================================================"
if [ "$FAILED" -eq 0 ]; then
    echo -e "${GREEN}>>> GATE CHECK PASSED: All requirements satisfied. <<<${NC}"
    exit 0
else
    echo -e "${RED}>>> GATE CHECK FAILED: Please fix violations before committing. <<<${NC}"
    exit 1
fi
