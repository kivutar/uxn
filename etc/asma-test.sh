#!/bin/sh
set -e
cd "$(dirname "${0}")/.."
rm -rf asma-test
mkdir asma-test
cd asma-test

expect_failure() {
	cat > 'in.tal'
	echo asma-test/in.tal | ( cd .. && bin/uxncli asma-test/asma.rom ) > out.rom 2> asma.log
	if ! grep -qF "${1}" asma.log; then
		echo "error: asma didn't report error ${1} in faulty code"
		xxd asma.log
	fi
}

echo 'Assembling asma with uxnasm'
( cd .. && bin/uxnasm projects/software/asma.tal asma-test/asma.rom ) > uxnasm.log
for F in $(find ../projects -path ../projects/library -prune -false -or -type f -name '*.tal' -not -name 'blank.tal' | sort); do
	echo "Comparing assembly of ${F}"
	BN="$(basename "${F%.tal}")"

	if ! ( cd .. && bin/uxnasm "asma-test/${F}" "asma-test/uxnasm-${BN}.rom" ) > uxnasm.log; then
		echo "error: uxnasm failed to assemble ${F}"
		tail uxnasm.log
		exit 1
	fi
	xxd "uxnasm-${BN}.rom" > "uxnasm-${BN}.hex"

	cp "${F}" 'in.tal'
	rm -f 'out.rom'
	echo asma-test/in.tal | ( cd .. && bin/uxncli asma-test/asma.rom ) > out.rom 2> asma.log
	cat asma.log
	if ! grep -qF 'bytes of heap used' asma.log; then
		echo "error: asma failed to assemble ${F}, while uxnasm succeeded"
		tail asma.log
		exit 1
	fi
	xxd 'out.rom' > "asma-${BN}.hex"

	diff -u "uxnasm-${BN}.hex" "asma-${BN}.hex"
done
expect_failure 'Invalid hexadecimal: $defg' <<'EOD'
|1000 $defg
EOD
expect_failure 'Invalid hexadecimal: #defg' <<'EOD'
|1000 #defg
EOD
expect_failure 'Address not in zero page: .hello' <<'EOD'
|1000 @hello
	.hello
EOD
expect_failure 'Address outside range: ,hello' <<'EOD'
|1000 @hello
|2000 ,hello
EOD
expect_failure 'Label not found: hello' <<'EOD'
hello
EOD
expect_failure 'Macro already exists: %abc' <<'EOD'
%abc { def }
%abc { ghi }
EOD
expect_failure 'Memory overwrite: SUB' <<'EOD'
|2000 ADD
|1000 SUB
EOD

echo 'All OK'

