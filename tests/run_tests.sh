#!/usr/bin/env bash
#
# run_tests.sh — тесты лексера и парсера mysh.
#
# Запуск:  bash tests/run_tests.sh [путь/к/mysh]     (по умолчанию ./mysh)
#          make test
# Для проверки на утечки и ошибки памяти:  bash tests/run_tests.sh ./mysh-debug
#
# Три группы проверок:
#   1. syntax.txt  — «принято / отвергнуто» для --dump-ast -c (код 0 или 2),
#                    результат сверяется ещё и с `bash -n`;
#   2. многострочные команды — то же, но со значащими переводами строки;
#   3. golden.txt  — точное сравнение вывода --dump-tokens и --dump-ast.
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

echo
echo "Проверок: $total, провалено: $failed"
[ "$failed" -eq 0 ]