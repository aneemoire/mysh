#!/usr/bin/env bash
#
# run_tests.sh — тесты лексера и парсера mysh.
#
# Запуск:  bash tests/run_tests.sh [путь/к/mysh]     (по умолчанию ./mysh)
#          make test
# Для проверки на утечки и ошибки памяти:  bash tests/run_tests.sh ./mysh-debug
#
# Шесть групп проверок:
#   1. syntax.txt  — «принято / отвергнуто» для --dump-ast -c (код 0 или 2),
#                    результат сверяется ещё и с `bash -n`;
#   2. многострочные команды — то же, но со значащими переводами строки;
#   3. golden.txt  — точное сравнение вывода --dump-tokens и --dump-ast.
#   4. выполнение  — `mysh -c` и `bash -c` на одних и тех же командах:
#                    совпадают код возврата, stdout и текст ошибки.
#   5. скрипты на stdin — `mysh < ввод` и `bash < ввод`: код возврата и stdout.
#   6. интерактивный режим через псевдотерминал (утилита script): Ctrl-C,
#      Ctrl-Z, jobs, fg, bg, уведомления о заданиях, выход.
# Группа 4 разбита на подгруппы: простые команды, перенаправления, списки,
# конвейеры, встроенные команды, подоболочки, фон.
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

check_rc() {
    local cmd=$1 brc mrc bout mout

    total=$((total + 1))
    bout=$(bash -c "$cmd" 2>/dev/null </dev/null); brc=$?
    mout=$("$MYSH" -c "$cmd" 2>/dev/null </dev/null); mrc=$?
    if [ "$brc" -ne "$mrc" ]; then
        fail "код возврата: bash $brc, mysh $mrc: $(printf '%q' "$cmd")"
    elif [ "$bout" != "$mout" ]; then
        fail "stdout отличается от bash: $(printf '%q' "$cmd")"
    fi
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

echo "== 4в. списки, && и ||: сверка с bash =="
 
check_run 'true && echo yes'
check_run 'false && echo yes'
check_run 'true || echo no'
check_run 'false || echo yes'
check_run 'false && echo a || echo b'
check_run 'true && echo a || echo b'
check_run 'true && false || echo b'
check_run 'false || true && echo c'
check_run 'false || false || echo c'
check_run 'true && true && false'
check_run 'false && false && true'
check_run 'echo a; echo b'
check_run 'echo a; echo b; echo c'
check_run 'true; false'
check_run 'false; true'
check_run 'echo a;'
check_run 'false && echo a; echo b'
check_run 'true || echo a; false'
check_run 'false || echo a; echo b && echo c'
check_run "sh -c 'exit 3' || echo got"
check_run "sh -c 'exit 3' && echo not_shown"
check_run "sh -c 'exit 3'; echo after"
check_run 'nosuchcmd_xyz && echo a'
check_run 'nosuchcmd_xyz || echo a'
check_run 'nosuchcmd_xyz; echo next'
check_run 'true; nosuchcmd_xyz'
check_run '/etc; echo a'
check_run 'echo a
echo b'
check_files '' 'echo a > f; echo b >> f'
check_files '' 'echo a > f && echo b >> f'
check_files '' 'false && echo a > f'
check_files '' 'cat < nosuchfile || echo b > g'
check_files '' 'cat < nosuchfile && echo b > g; echo c > h'

echo "== 4г. конвейеры: сверка с bash =="

check_run 'echo hi | cat'
check_run 'echo a b c | wc -w'
check_run "printf 'b\\na\\nc\\n' | sort"
check_run 'echo hi | cat | cat | cat'
check_run "echo x$(printf ' | cat%.0s' $(seq 1 50))"
check_run 'seq 1 100000 | wc -l'
check_run 'yes | head -3'
check_run 'true | cat'
check_run 'false | true'
check_run 'true | false'
check_run "sh -c 'exit 3' | sh -c 'exit 4'"
check_run "sh -c 'exit 3' | cat"
check_run "echo x | sh -c 'cat >/dev/null; exit 5'"
check_run "sh -c 'kill -KILL \$\$' | cat"
check_rc "echo x | sh -c 'kill -TERM \$\$'"
check_run 'nosuchcmd_xyz | cat'
check_run 'echo hi | nosuchcmd_xyz'
check_rc 'nosuchcmd_xyz | nosuchcmd_abc'
check_run 'echo hi | /etc'
check_run 'echo a | cat && echo b'
check_run 'false | cat || echo c'
check_run 'echo a | cat; echo b | cat'
check_run 'echo a | cat | wc -l; echo done'
check_files 'echo data > in' 'cat < in | wc -l > out'
check_files '' 'echo hi | cat > out'
check_files '' 'echo a > f | cat'
check_files '' 'echo a | cat > f | cat'
check_files 'echo data > in' 'cat < in | cat < in'
check_files '' 'cat < nosuchfile | echo x'
check_files '' 'echo x | cat > nosuchdir/out'
check_files '' 'echo a | cat >> f; echo b | cat >> f'
 
total=$((total + 1))
t0=$(date +%s%N)
"$MYSH" -c 'sleep 1 | sleep 1 | sleep 1' </dev/null
t1=$(date +%s%N)
[ $(((t1 - t0) / 1000000)) -lt 2000 ] || fail "команды конвейера выполняются не параллельно: $(((t1 - t0) / 1000000)) мс"

total=$((total + 1))
base=$(printf 'ls /proc/self/fd\n' | "$MYSH" 2>&1)
many=$({ for _ in $(seq 1 200); do printf 'echo x | cat > /dev/null\n'; done; printf 'ls /proc/self/fd\n'; } | "$MYSH" 2>&1)
[ "$base" = "$many" ] || fail "утечка файловых дескрипторов после конвейеров: было '$(echo $base)', стало '$(echo $many)'"

if command -v ps >/dev/null 2>&1; then
    total=$((total + 1))
    got=$("$MYSH" -c "echo a | cat | cat > /dev/null; sh -c 'ps -o stat= --ppid \$PPID'" </dev/null 2>&1)
    case $got in
        *Z*) fail "после конвейера остались зомби: $got" ;;
    esac
fi

echo "== 4д. встроенные команды: сверка с bash =="

check_run 'echo hello world'
check_run 'echo -n hello'
check_run 'echo -n'
check_run 'echo -n -n x'
check_run 'echo -nx'
check_run 'echo a    b'
check_run 'echo "a  b" c'
check_run 'echo "" x'
check_run 'echo --'
check_run 'echo a\\nb'
check_run 'echo hi | cat'
check_run 'echo hi | cat | cat'
check_run 'pwd'
check_run 'pwd | cat'
check_run 'cd /tmp; pwd'
check_run 'cd /; pwd; cd /tmp; pwd'
check_run 'cd /tmp && pwd'
check_run 'cd /nonexistent_dir_xyz'
check_run 'cd /nonexistent_dir_xyz || echo failed'
check_run 'cd /etc/passwd'
check_run 'cd /tmp /'
check_run 'cd /tmp | pwd'
check_run "cd /; sh -c 'echo \$PWD'"
check_run 'exit 3'
check_run 'exit'
check_run 'false; exit'
check_run 'exit 256'
check_run 'exit 257'
check_run 'exit -1'
check_run 'exit 0; echo no'
check_run 'exit abc'
check_run 'exit 1 2; echo after'
check_run 'exit abc def'
check_run 'echo a; exit 4; echo b'
check_run 'true && exit 5'
check_run 'exit 3 | cat'
check_run 'cat < /dev/null | exit 4'
check_run 'echo x | exit 2'
check_run 'help >/dev/null'
check_run 'nosuchcmd_xyz; exit'
check_run 'echo hi > /dev/full'
check_rc 'pwd -x'
check_run 'pwd extra'
check_run 'echo a; cd /nonexistent_dir_xyz; echo b'
check_files '' 'echo hi > out'
check_files '' 'echo hi >> out; echo more >> out'
check_files '' 'echo -n x > out'
check_files 'echo old > f' 'echo new > f; echo after'
check_files '' 'echo hi > nosuchdir/out'
check_files '' 'echo hi > a > b'
check_files '' 'echo a > f && echo b >> f'
check_files 'echo data > in' 'cat < in | cat > out'
check_files '' 'echo a | cat > out; echo b'
check_files '' 'cd /nonexistent_dir_xyz > out'

total=$((total + 1))
bwant=$(HOME=/tmp bash -c 'cd; pwd' 2>&1)
mgot=$(HOME=/tmp "$MYSH" -c 'cd; pwd' 2>&1)
[ "$bwant" = "$mgot" ] || fail "cd без аргумента: bash '$bwant', mysh '$mgot'"
total=$((total + 1))
bwant=$(env -u HOME bash -c 'cd' 2>&1; echo "rc=$?")
mgot=$(env -u HOME "$MYSH" -c 'cd' 2>&1; echo "rc=$?")
[ "$(printf '%s' "$bwant" | strip_prefix)" = "$(printf '%s' "$mgot" | strip_prefix)" ] ||
    fail "cd без HOME: bash '$bwant', mysh '$mgot'"

echo "== 4е. подоболочки: сверка с bash =="

check_run '(echo a)'
check_run '(echo a; echo b)'
check_run '(echo a; echo b) | wc -l'
check_run 'echo x | (cat; echo y)'
check_run '(echo a | cat; echo b) | cat'
check_run '(cd /; pwd); pwd'
check_run '(cd /tmp); pwd'
check_run '(exit 3)'
check_run '(exit 3); echo after'
check_run '(exit 3) || echo failed'
check_run '(false; true)'
check_run '(true; false)'
check_run '(false)'
check_run '( (echo a) )'
check_run '( (echo a; echo b) | cat )'
check_run '( ( ( ( ( ( ( ( ( ( echo deep ) ) ) ) ) ) ) ) ) )'
check_run '(echo a && echo b)'
check_run '(false && echo a || echo b)'
check_run '(exit 3; echo no)'
check_run '( (exit 2) )'
check_run '(nosuchcmd_xyz)'
check_run '(nosuchcmd_xyz) || echo failed'
check_run '(echo a) && (echo b) ; (echo c)'
check_run '(echo a) | (cat) | (cat)'
check_run '(exit 2) | (exit 3)'
check_run '(echo a; exit 5; echo b)'
check_run '(echo a
echo b)'
check_files '' '(echo a; echo b) > out'
check_files '' '(echo a; echo b) >> out'
check_files 'echo data > in' '(cat) < in'
check_files 'echo data > in' '(cat; echo y) < in > out'
check_files '' '(echo a > f; echo b >> f)'
check_files '' '(echo a) > out | cat'
check_files '' '(echo a > f1) > f2'
check_files '' '(cat < nosuchfile) > out'
check_files '' '(echo a) > nosuchdir/out'
check_files '' '(echo a; echo b) > out; echo c'

echo "== 4ж. фон, jobs, wait, kill (без терминала): сверка с bash =="

check_run 'echo a & wait; echo b'
check_run 'sleep 0.1 & wait; echo done'
check_run 'sleep 0.1 & echo started; wait'
check_run 'sleep 0.1 & sleep 0.1 & wait; echo done'
check_run 'false & wait %1'
check_run 'true & wait %1'
check_run "sh -c 'exit 5' & wait %1"
check_run "sh -c 'exit 5' & wait %1; echo after"
check_run 'echo a & echo b & wait'
check_run 'true & true; wait; echo ok'
check_run 'wait'
check_run 'wait %1'
check_run 'wait %5'
check_run 'wait 99999'
check_run 'wait abc'
check_run 'true && echo a & wait'
check_run '(sleep 0.1; echo a) & wait; echo b'
check_run '(sleep 0.1; echo a) & echo b; wait'
check_run 'echo a | cat & wait; echo b'
check_run 'echo a | cat | cat & wait'
check_run 'sleep 0.1 & jobs; wait'
check_run 'sleep 0.1 & sleep 0.2 & jobs; wait'
check_run 'sleep 0.1 & wait; jobs'
check_run 'sleep 0.1 & sleep 0.4; jobs'
check_run 'sleep 0.1 & sleep 0.1 & sleep 0.4; jobs'
check_run 'sh -c "exit 3" & sleep 0.3; wait %1'
check_run 'sh -c "exit 3" & sleep 0.3; jobs; wait %1'
check_run 'sh -c "exit 3" & sleep 0.3; wait'
check_run 'sleep 0.1 & sleep 0.4; echo x; jobs; echo y'
check_run 'sleep 0.1 && echo x & jobs; wait'
check_run 'jobs'
check_run 'fg'
check_run 'bg'
check_run 'sleep 0.1 & fg'
check_run 'sleep 0.1 & bg'
check_rc 'sleep 5 & kill %1; wait %1'
check_rc 'sleep 5 & kill -9 %1; wait %1'
check_rc 'sleep 5 & kill -KILL %1; wait %1'
check_rc 'sleep 5 & kill -s SIGTERM %1; wait %1'
check_rc 'sleep 5 & kill -TERM %1; wait %1'
check_run 'kill %1'
check_rc 'kill'
check_run 'kill -s'
check_run 'kill -NOSUCH 1'
check_run 'kill abc'
check_run 'kill 99999999'
check_run 'kill -0 1 || true'
check_run 'sleep 0.1 & kill -0 %1; wait'
check_files '' 'echo a > f & wait'
check_files '' 'echo a > f & wait; echo b >> f'
check_files '' '(echo a; echo b) > out & wait'

total=$((total + 1))
got=$(printf 'data\n' | "$MYSH" -c 'cat & wait; echo end' 2>&1)
want=$(printf 'data\n' | bash -c 'cat & wait; echo end' 2>&1)
[ "$got" = "$want" ] || fail "фоновая команда читает stdin: bash '$want', mysh '$got'"

total=$((total + 1))
t0=$(date +%s%N)
"$MYSH" -c 'sleep 1 & echo x' </dev/null >/dev/null
t1=$(date +%s%N)
[ $(((t1 - t0) / 1000000)) -lt 800 ] || fail "фоновая команда блокирует интерпретатор: $(((t1 - t0) / 1000000)) мс"

if command -v ps >/dev/null 2>&1; then
    total=$((total + 1))
    got=$("$MYSH" -c "sleep 0.1 & sleep 0.5; sh -c 'ps -o stat= --ppid \$PPID'" </dev/null 2>&1)
    case $got in
        *Z*) fail "после фонового задания остались зомби: $got" ;;
    esac
fi

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
check_stdin $'echo a\nexit 3\necho b\n'
check_stdin $'false\nexit\n'
check_stdin $'cd /tmp\npwd\n'
check_stdin $'(cd /; pwd)\npwd\n'
check_stdin $'echo a | cat\necho b | cat\n'
check_stdin $'sleep 0.1 &\nwait\necho x\n'
check_stdin $'cat &\nwait\necho x\n'
check_stdin $'echo a > /dev/null\necho b\n'
check_stdin $'exit 2\n'

echo "== 6. интерактивный режим (псевдотерминал) =="

pty_run() {
    local a
    PTY_OUT=$({
        for a in "$@"; do
            case $a in
                @*) sleep "${a#@}" ;;
                *)  printf '%b' "$a" ;;
            esac
        done
    } | script -qefc "$MYSH" /dev/null 2>&1 | tr -d '\r'; exit "${PIPESTATUS[1]}")
    PTY_RC=$?
}

pty_expect() {
    total=$((total + 1))
    printf '%s\n' "$PTY_OUT" | grep -Eq -- "$2" ||
        fail "интерактивный режим, $1: нет '$2' в выводе $(printf '%q' "$PTY_OUT")"
}

pty_reject() {
    total=$((total + 1))
    if printf '%s\n' "$PTY_OUT" | grep -Eq -- "$2"; then
        fail "интерактивный режим, $1: лишнее '$2' в выводе $(printf '%q' "$PTY_OUT")"
    fi
}

pty_rc() {
    total=$((total + 1))
    [ "$PTY_RC" -eq "$2" ] || fail "интерактивный режим, $1: код выхода $PTY_RC, ожидался $2"
}

if command -v script >/dev/null 2>&1; then
    pty_run 'echo hello\n' @0.2 'echo a | cat\n' @0.2 'exit 3\n' @0.2
    pty_expect 'простая команда' '(^|> )hello$'
    pty_expect 'конвейер' '^a$'
    pty_rc 'exit 3' 3

    pty_run @0.3 '\003' @0.2 'echo alive\n' @0.2 'exit\n' @0.2
    pty_expect 'Ctrl-C на приглашении не мешает работе' '^alive$'

    pty_run 'sleep 100\n' @0.4 '\003' @0.3 'echo alive\n' @0.2 'jobs\n' @0.2 'exit\n' @0.2
    pty_expect 'Ctrl-C прерывает задание переднего плана' '^alive$'
    pty_reject 'прерванное задание не остаётся в jobs' 'Running|Stopped'

    pty_run 'sleep 100\n' @0.4 '\032' @0.3 'jobs\n' @0.3 'bg\n' @0.3 'jobs\n' @0.3 \
            'fg\n' @0.4 '\003' @0.3 'echo alive\n' @0.2 'exit 5\n' @0.2
    pty_expect 'Ctrl-Z останавливает задание' '\[1\]\+ +Stopped +sleep 100'
    pty_expect 'bg выводит строку задания' '^\[1\]\+ sleep 100 &$'
    pty_expect 'jobs показывает Running' '\[1\]\+ +Running +sleep 100 &'
    pty_expect 'fg печатает команду' '^sleep 100$'
    pty_expect 'после fg и Ctrl-C интерпретатор работает' '^alive$'
    pty_rc 'выход после заданий' 5

    pty_run 'sleep 0.3 &\n' @0.8 'echo x\n' @0.2 'jobs\n' @0.2 'exit\n' @0.2
    pty_expect 'строка запуска в фоне' '(^|> )\[1\] [0-9]+$'
    pty_expect 'уведомление Done перед приглашением' '\[1\]\+ +Done +sleep 0.3'
    pty_reject 'уведомлённое задание удалено' '^\[1\][+-] +Done.*\n.*Done'

    pty_run 'sleep 100 &\n' 'sleep 101 &\n' 'jobs\n' @0.2 'kill %1 %2\n' @0.4 'echo x\n' @0.2 'jobs\n' 'exit\n' @0.2
    pty_expect 'маркер - у предыдущего задания' '\[1\]- +Running +sleep 100 &'
    pty_expect 'маркер + у текущего задания' '\[2\]\+ +Running +sleep 101 &'
    pty_expect 'уведомление о завершении по сигналу' 'Terminated +sleep 10[01]'

    pty_run 'sleep 100 &\n' 'kill %1\n' 'wait %1\n' 'echo ok\n' @0.2 'sleep 0.2 &\n' 'wait\n' 'echo ok2\n' 'exit\n' @0.2
    pty_expect 'wait после kill' '(^|> )ok$'
    pty_expect 'wait без аргументов' '(^|> )ok2$'

    pty_run 'fg\n' 'bg\n' 'fg %7\n' 'exit\n' @0.2
    pty_expect 'fg и bg без заданий' 'fg: current: no such job'
    pty_expect 'bg без заданий' 'bg: current: no such job'
    pty_expect 'fg с неверным номером' 'fg: %7: no such job'

    pty_run 'sleep 100\n' @0.4 '\032' @0.3 'exit\n' @0.3 'exit 5\n' @0.3
    pty_expect 'exit с остановленным заданием предупреждает' 'stopped jobs'
    pty_rc 'повторный exit завершает работу' 5

    pty_run 'sleep 100\n' @0.4 '\032' @0.3 '\004' @0.3 '\004' @0.3
    pty_expect 'Ctrl-D с остановленным заданием предупреждает' 'stopped jobs'
    pty_rc 'повторный Ctrl-D завершает работу (код последней команды)' 148

    pty_run @0.2 '\004' @0.3
    pty_expect 'Ctrl-D печатает exit' '(^|> )exit$'
    pty_rc 'Ctrl-D' 0

    pty_run 'sleep 100 | cat\n' @0.4 '\032' @0.3 'jobs\n' @0.3 'fg\n' @0.4 '\003' @0.3 'echo alive\n' @0.2 'exit\n' @0.2
    pty_expect 'конвейер останавливается целиком' '\[1\]\+ +Stopped +sleep 100 \| cat'
    pty_expect 'fg конвейера' '^sleep 100 \| cat$'
    pty_expect 'после Ctrl-C в конвейере интерпретатор работает' '^alive$'

    pty_run '(echo a; echo b) | cat\n' @0.2 'sleep 0.2 && echo done &\n' @0.2 @0.6 'echo x\n' @0.2 'exit\n' @0.2
    pty_expect 'подоболочка в конвейере' '^b$'
    pty_expect 'цепочка в фоне выполнилась' '(^|> )done$'
    pty_expect 'цепочка в фоне - одно задание' '\[1\] [0-9]+'
else
    echo "утилита script не найдена: группа пропущена"
fi

echo
echo "Проверок: $total, провалено: $failed"
[ "$failed" -eq 0 ]