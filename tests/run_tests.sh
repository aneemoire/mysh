#!/usr/bin/env bash
#
# run_tests.sh — тесты лексера и парсера mysh.
#
# Запуск:  bash tests/run_tests.sh [путь/к/mysh]     (по умолчанию ./mysh)
#          make test
# Для проверки на утечки и ошибки памяти:  bash tests/run_tests.sh ./mysh-debug
#
# Пять групп проверок:
#   1. syntax.txt  — «принято / отвергнуто» для --dump-ast -c (код 0 или 2),
#                    результат сверяется ещё и с `bash -n`;
#   2. многострочные команды — то же, но со значащими переводами строки;
#   3. golden.txt  — точное сравнение вывода --dump-tokens и --dump-ast.
#   4. выполнение  — `mysh -c` и `bash -c` на одних и тех же командах:
#                    совпадают код возврата, stdout и текст ошибки.
#   5. скрипты на stdin — `mysh < ввод` и `bash < ввод`: код возврата и stdout.
# Интерактивный режим (приглашения, Ctrl-D) автоматически не проверяется
# Код возврата скрипта: 0 — всё прошло, 1 — есть провалы.

MYSH=${1:-./mysh}
DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [ ! -x "$MYSH" ]; then
    echo "Не найден исполняемый файл: $MYSH (сначала выполните make)" >&2
    exit 1
fi

total=0
failed=0

fail() {
    failed=$((failed + 1))
    echo "ПРОВАЛ: $1"
}

check_syntax() {
    local label=$1 cmd=$2 want rc bash_rc

    total=$((total + 1))
    case $label in
        ok)       want=0 ;;
        err|dev)  want=2 ;;
        *)        fail "неизвестная метка '$label' у команды: $cmd"; return ;;
    esac

    "$MYSH" --dump-ast -c "$cmd" >/dev/null 2>&1
    rc=$?
    if [ "$rc" -ne "$want" ]; then
        fail "mysh вернул $rc, ожидалось $want: $(printf '%q' "$cmd")"
        return
    fi

    if [ "$label" != dev ] && command -v bash >/dev/null 2>&1; then
        bash -n -c "$cmd" >/dev/null 2>&1
        bash_rc=$?
        if { [ "$label" = ok ] && [ "$bash_rc" -ne 0 ]; } ||
           { [ "$label" = err ] && [ "$bash_rc" -eq 0 ]; }; then
            fail "метка '$label' расходится с bash -n (код $bash_rc): $(printf '%q' "$cmd")"
        fi
    fi
}

echo "== 1. syntax.txt: принято / отвергнуто =="
while IFS= read -r line || [ -n "$line" ]; do
    case $line in
        ''|'#'*) continue ;; 
    esac
    label=${line%% *}
    cmd=${line#"$label"}
    cmd=${cmd# }
    check_syntax "$label" "$cmd"
done < "$DIR/syntax.txt"

echo "== 2. многострочные команды =="
check_syntax ok  $'echo a\necho b'
check_syntax ok  $'echo a\n\n\necho b\n'
check_syntax ok  $'echo a ;\necho b'
check_syntax ok  $'echo a\n'
check_syntax ok  $'\n\n'
check_syntax ok  $'(\necho a\necho b\n)'
check_syntax ok  $'echo "a\nb"'
check_syntax err $'echo a ;\n; echo b'

echo "== 3. golden.txt: точный вывод =="
run_golden() {
    local name=$1 mode=$2 cmd=$3 want=$4 got

    total=$((total + 1))
    got=$("$MYSH" "$mode" -c "$cmd" 2>&1)
    if [ "$got" != "$want" ]; then
        fail "golden '$name' ($mode)"
        diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") | sed 's/^/    /'
    fi
}

name="" mode="" cmd="" want="" state=none
flush() {
    if [ -n "$name" ]; then
        cmd=${cmd%$'\n'}
        while [[ $want == *$'\n' ]]; do want=${want%$'\n'}; done
        run_golden "$name" "$mode" "$cmd" "$want"
    fi
}
while IFS= read -r line || [ -n "$line" ]; do
    case $line in
        '=== '*)
            flush
            header=${line#=== }
            name=${header%% *}
            mode=${header##* }
            cmd="" want="" state=cmd ;;
        '--- expected')
            state=want ;;
        *)
            case $state in
                cmd)  cmd+="$line"$'\n' ;;
                want) want+="$line"$'\n' ;;
            esac ;;
    esac
done < "$DIR/golden.txt"
flush

echo "== 4. выполнение команд: сверка с bash =="

strip_prefix() { sed -E 's/^(bash|mysh): (line [0-9]+: )?//'; }
 
check_run() {
    local cmd=$1 brc mrc
    local bout berr mout merr
    bout=$(mktemp) berr=$(mktemp) mout=$(mktemp) merr=$(mktemp)
 
    total=$((total + 1))

    { bash -c "$cmd" >"$bout" 2>"$berr" </dev/null; } 2>/dev/null; brc=$?
    { "$MYSH" -c "$cmd" >"$mout" 2>"$merr" </dev/null; } 2>/dev/null; mrc=$?
    if [ "$brc" -ne "$mrc" ]; then
        fail "код возврата: bash $brc, mysh $mrc: $(printf '%q' "$cmd")"
    elif ! cmp -s "$bout" "$mout"; then
        fail "stdout отличается от bash: $(printf '%q' "$cmd")"
    elif [ "$(strip_prefix <"$berr")" != "$(strip_prefix <"$merr")" ]; then
        fail "текст ошибки отличается от bash: $(printf '%q' "$cmd")"
    fi
    rm -f "$bout" "$berr" "$mout" "$merr"
}
 
check_run 'true'
check_run 'false'
check_run 'echo hello world'
check_run "printf '%s|' 'a b' c"
check_run '/bin/echo "a  b"'
check_run 'echo'
check_run ''
check_run '# только комментарий'
check_run "sh -c 'exit 3'"
check_run "sh -c 'exit 255'"
check_run "sh -c 'echo out; echo err >&2; exit 4'"
check_run "sh -c 'kill -TERM \$\$'"               
check_run "sh -c 'kill -KILL \$\$'"
check_run 'ls /nonexistent_dir_xyz'
check_run 'nosuchcmd_xyz'
check_run '/nonexistent'
check_run './nonexistent'
check_run "''"
check_run '/etc'
check_run '/etc/passwd'
check_run '/etc/passwd/x'
 
total=$((total + 1))
got=$(TEST_MYSH_VAR=значение "$MYSH" -c "sh -c 'echo \$TEST_MYSH_VAR'" </dev/null 2>&1)
[ "$got" = "значение" ] || fail "окружение не передаётся дочернему процессу (получено '$got')"
 
total=$((total + 1))
got=$(printf 'привет\n' | "$MYSH" -c cat 2>&1)
[ "$got" = "привет" ] || fail "stdin не передаётся дочернему процессу (получено '$got')"
 
total=$((total + 1))
args=$(seq 1 3000 | tr '\n' ' ')
got=$("$MYSH" -c "printf '%s\n' $args" </dev/null 2>&1 | wc -l)
[ "$got" -eq 3000 ] || fail "3000 аргументов: передано $got"

echo "== 4б. перенаправления: сверка с bash =="
 
dir_state() {
    local f
    for f in $(ls -A); do
        printf '== %s (%s)\n' "$f" "$(stat -c %a "$f")"
        cat "$f" 2>&1
    done
}
 
check_files() {
    local setup=$1 cmd=$2 tmp brc mrc
    local bout berr mout merr bstate mstate
 
    total=$((total + 1))
    tmp=$(mktemp -d)
    bout=$tmp/bout berr=$tmp/berr mout=$tmp/mout merr=$tmp/merr
 
    mkdir "$tmp/b" "$tmp/m"
    (cd "$tmp/b" && bash -c "$setup" >/dev/null 2>&1 </dev/null)
    (cd "$tmp/m" && bash -c "$setup" >/dev/null 2>&1 </dev/null)
    (cd "$tmp/b" && { bash -c "$cmd" >"$bout" 2>"$berr" </dev/null; } 2>/dev/null)
    brc=$?
    (cd "$tmp/m" && { "$MYSH" -c "$cmd" >"$mout" 2>"$merr" </dev/null; } 2>/dev/null)
    mrc=$?
    bstate=$(cd "$tmp/b" && dir_state)
    mstate=$(cd "$tmp/m" && dir_state)
 
    if [ "$brc" -ne "$mrc" ]; then
        fail "перенаправление: код возврата bash $brc, mysh $mrc: $(printf '%q' "$cmd")"
    elif ! cmp -s "$bout" "$mout"; then
        fail "перенаправление: stdout отличается от bash: $(printf '%q' "$cmd")"
    elif [ "$(strip_prefix <"$berr")" != "$(strip_prefix <"$merr")" ]; then
        fail "перенаправление: текст ошибки отличается от bash: $(printf '%q' "$cmd")"
    elif [ "$bstate" != "$mstate" ]; then
        fail "перенаправление: файлы отличаются от bash: $(printf '%q' "$cmd")"
    fi
    rm -rf "$tmp"
}
 
case $MYSH in
    /*) ;;
    *)  MYSH=$PWD/$MYSH ;;
esac
 
check_files '' 'echo hi > out'
check_files '' '/bin/echo hi > out'
check_files '' '> out echo hi'
check_files '' 'echo a > out1 b > out2 c'
check_files 'echo old > f' 'echo new > f'
check_files 'echo old > f' 'echo new >> f'
check_files '' 'echo one >> f'
check_files 'echo old longer text > f' 'echo x > f'
check_files 'echo data > in' 'cat < in'
check_files 'echo data > in' 'cat < in > out'
check_files 'echo data > in' '< in cat > out'
check_files 'echo a > f; echo b > g' 'cat < f < g'
check_files '' 'cat < nosuchfile'
check_files '' 'cat < nosuchfile > out'
check_files '' 'echo hi > nosuchdir/out'
check_files '' 'echo hi > /etc'
check_files '' 'echo hi > out < nosuchfile'
check_files '' 'nosuchcmd_xyz > out'
check_files '' 'echo hi > a > b'
check_files 'echo x > f; chmod 000 f' 'cat < f'
check_files 'echo x > f; chmod 444 f' 'echo y > f'

echo "== 5. команды из stdin (mysh < файл): сверка с bash =="

check_stdin() {
    local input=$1 brc mrc bout mout
    bout=$(mktemp) mout=$(mktemp)
 
    total=$((total + 1))
    printf '%s' "$input" | bash >"$bout" 2>/dev/null; brc=$?
    printf '%s' "$input" | "$MYSH" >"$mout" 2>/dev/null; mrc=$?
    if [ "$brc" -ne "$mrc" ]; then
        fail "stdin: код возврата bash $brc, mysh $mrc: $(printf '%q' "$input")"
    elif ! cmp -s "$bout" "$mout"; then
        fail "stdin: stdout отличается от bash: $(printf '%q' "$input")"
    fi
    rm -f "$bout" "$mout"
}
 
check_stdin $'echo one\necho two\n'
check_stdin $'true\nfalse\n' 
check_stdin $'false\ntrue\n'
check_stdin $'echo one\n\n\necho two\n'
check_stdin $'# комментарий\necho x\n'
check_stdin $'echo "multi\nline"\necho after\n'
check_stdin $'echo a\\\nb\n'
check_stdin $'echo one\necho a ; ; b\necho three\n'
check_stdin $'nosuchcmd_xyz\necho next\n'
check_stdin ''
check_stdin 'echo без перевода строки в конце'
check_stdin $'false\n\n'
check_stdin $'false\n# комментарий\n'
check_stdin $'echo "abc\n'
check_stdin $'cat\nпривет\nмир\n'
check_stdin $'sh -c \'exit 7\'\n'
check_stdin $'echo one\nsh -c \'exit 3\'\n\n\n'

echo
echo "Проверок: $total, провалено: $failed"
[ "$failed" -eq 0 ]