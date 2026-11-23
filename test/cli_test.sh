# Copyright 2026 Ian Karlsson
# synthfs, the command line, on images made here: a FAT floppy changed and read back, a file of exclusive messages
set -u
synthfs=$1
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir" || exit 1
failures=0
check() {
	if ! eval "$1"; then
		echo "failed: $1" >&2
		failures=$((failures + 1))
	fi
}

# a FAT floppy: a file put in, a directory made, the file moved into it and named anew, read back
check '"$synthfs" create -l TEST fat 720K fat.img'
printf 'hello\n' > a.txt
check '"$synthfs" add fat.img a.txt'
check '"$synthfs" mkdir fat.img SUB'
check '"$synthfs" mv fat.img a.txt SUB/B.TXT'
check '[ "$("$synthfs" ls fat.img)" = "$(printf "FAT            floppy 1 directory 1 file  fat.img  TEST\n  directory      1  SUB             ")" ]'
check '"$synthfs" ls -l fat.img | grep -q "^    file           1  B.TXT             6 bytes$"'
check '[ "$("$synthfs" export fat.img SUB/B.TXT)" = hello ]'
check '[ "$("$synthfs" export fat.img -t raw /SUB/./../SUB/file:1)" = hello ]'
check '"$synthfs" info fat.img SUB/B.TXT | grep -q "^exports      raw$"'
check '! "$synthfs" rm fat.img NONE 2> /dev/null'
check '! "$synthfs" export fat.img -o a.txt SUB/B.TXT 2> /dev/null'
# a VHD of it and back
check '"$synthfs" convert fat.img fat.vhd > /dev/null && "$synthfs" convert fat.vhd back.img > /dev/null && cmp -s fat.img back.img'
check '[ "$("$synthfs" scan fat.vhd)" = "FAT            floppy 1 directory 1 file  fat.vhd  TEST" ]'

# the shell: piped, no prompt; it stands where cd puts it, and stops at the first command that fails
out=$(printf 'cd SUB\npwd\nls -d B.TXT\n# a comment\nrm B.TXT\nls\n' | "$synthfs" shell -w fat.img)
check '[ "$out" = "$(printf "/SUB\nfile           1  B.TXT             6 bytes\ndirectory      1  SUB             ")" ]'
check '! printf "ls\nrm NONE\npwd\n" | "$synthfs" shell -w fat.img > out.txt 2> /dev/null'
check '[ $(wc -l < out.txt) = 2 ]'
check '! printf "mkdir X\n" | "$synthfs" shell fat.img 2> /dev/null'

# an S-760's sample as one Data Set: its name, the rest zero
units=""
sum=$((0x0C))
for c in $(printf 'SMP:Test        ' | od -An -tu1); do
	units+=$(printf '\\x%02x\\x%02x' $((c >> 4)) $((c & 15)))
	sum=$((sum + (c >> 4) + (c & 15)))
done
for k in $(seq 32); do
	units+='\x00\x00'
done
printf "\xf0\x41\x10\x34\x12\x00\x0c\x00\x00$units\\x$(printf %02x $(((128 - sum % 128) % 128)))\xf7" > one.syx
check '"$synthfs" ls one.syx | grep -q "^  sample         1  SMP:Test "'
check '"$synthfs" export one.syx -t syx -o again.syx / > /dev/null && cmp -s one.syx again.syx'
check '[ "$("$synthfs" export one.syx SMP:Test | wc -c)" = $(wc -c < one.syx) ]'
check '"$synthfs" export one.syx sample:1 -u -o twice.syx > /dev/null && "$synthfs" ls twice.syx | grep -q "1 sample"'

if [ $failures != 0 ]; then
	echo "$failures failed" >&2
	exit 1
fi
echo "all passed"
