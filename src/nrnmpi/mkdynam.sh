#!/bin/sh

names=`sed -n '
/extern /s/extern [a-z*]* \(nrnmpi_[a-zA-Z0-9_]*\)(.*);/\1/p
/BGPDMA/s/.*/BGPDMA/p
$s/.*/ENDIF/p
' nrnmpidec.h`

#generate nrnmpi_dynam_wrappers.inc
sed -n '
/extern void/s/extern \(void\) \(nrnmpi_[a-zA-Z0-9_]*\)\(.*\);/\1 \2\3 {@  (*p_\2)\3;@}/p
/extern [^v]/s/extern \([a-z*]*\) \(nrnmpi_[a-zA-Z0-9_]*\)\(.*\);/\1 \2\3 {@  return (*p_\2)\3;@}/p
/BGPDMA/p
$p
' nrnmpidec.h | tr '@' '\n' | sed '
/p_nrnmpi/ {
s/, [a-zA-Z_*]* /, /g
s/)([a-zA-Z_*]* /)(/
s/char\* //g
}
'> nrnmpi_dynam_wrappers.inc

#generate nrnmpi_dynam.h
(
echo '
#ifndef nrnmpi_dynam_h
#define nrnmpi_dynam_h
/* generated by mkdynam.sh */

#if NRNMPI_DYNAMICLOAD
'
for i in $names ; do
	if test "$i" = "BGPDMA" ; then
		echo "#if BGPDMA"
	elif test "$i" = "ENDIF" ; then
		echo "#endif"
	else
		echo "#define $i f_$i"
	fi
done

echo '
#endif /* NRNMPI_DYNAMICLOAD */

#endif
'
) > nrnmpi_dynam.h

#generate nrnmpi_dynam_cinc
(

sed -n '
/extern/s/extern \([a-z*]*\) \(nrnmpi_[a-zA-Z0-9_]*\)\(.*\);/static \1 (*p_\2)\3;/p
/BGPDMA/p
$p
' nrnmpidec.h
echo '
static struct {
	char* name;
	void** ppf;
}ftable[] = {'
for i in $names ; do
	if test "$i" = "BGPDMA" ; then
		echo "#if BGPDMA"
	elif test "$i" = "ENDIF" ; then
		echo "#endif"
	else
		echo "	\"f_$i\", (void**)&p_$i,"
	fi
done
echo '	0,0
};
'
) > nrnmpi_dynam_cinc

