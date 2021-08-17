#!/bin/sh
set -e
cd "$(dirname "${0}")/.."
rm -rf asma-test
mkdir asma-test
cd asma-test

build_asma() {
	sed -ne '/^( devices )/,/^( vectors )/p' ../projects/software/asma.tal
	cat <<EOD
|0100 @reset
	;&source-file ;&dest-file ;asma-assemble-file JSR2
	#01 .System/halt DEO
	BRK

	&source-file "in.tal 00
	&dest-file "out.rom 00

EOD
	sed -ne '/%asma-IF-ERROR/,$p' ../projects/software/asma.tal
}

expect_failure() {
	cat > 'in.tal'
	../bin/uxncli asma.rom > asma.log 2>/dev/null
	if ! grep -qF "${1}" asma.log; then
		echo "error: asma didn't report error ${1} in faulty code"
		tail asma.log
		exit 1
	fi
}

echo 'Assembling asma with uxnasm'
build_asma > asma.tal
../bin/uxnasm asma.tal asma.rom > uxnasm.log
for F in $(find ../projects -type f -name '*.tal' -not -name 'blank.tal'); do
	echo "Comparing assembly of ${F}"
	BN="$(basename "${F%.tal}")"

	if ! ../bin/uxnasm "${F}" "uxnasm-${BN}.rom" > uxnasm.log; then
		echo "error: uxnasm failed to assemble ${F}"
		tail uxnasm.log
		exit 1
	fi
	xxd "uxnasm-${BN}.rom" > "uxnasm-${BN}.hex"

	cp "${F}" 'in.tal'
	rm -f 'out.rom'
	../bin/uxncli asma.rom > asma.log
	if [ ! -f 'out.rom' ]; then
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

