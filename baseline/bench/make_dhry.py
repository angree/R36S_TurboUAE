# Derives dhry_1_amiga.c from the unmodified Dhrystone 2.1 dhry_1.c: main() becomes
# dhry_run(runs), which does the original initialisation and the original measured loop and
# returns 1 when the benchmark's own final values are right. Prompt, timing and the float
# report are dropped (the 68020 target has no FPU; amiga_bench.c times it in integers).
import io
L = io.open('dhry21/dhry_1.c', encoding='latin-1').read().split('\n')
def find(text, start=0):
    for i in range(start, len(L)):
        if text in L[i]: return i
    raise SystemExit('not found: ' + text)
m = find('main ()')
brace = find('{', m)
first_print = find('printf (', brace)
loop = find('for (Run_Index = 1;', first_print)
loop_end = find('} /* loop "for Run_Index" */', loop)
main_end = next(i for i in range(loop_end, len(L)) if L[i].startswith('}'))
out = L[:m]
out += ['int dhry_run (Runs_Arg)', 'int Runs_Arg;']
out += L[brace:first_print]
out += ['  Number_Of_Runs = Runs_Arg;', '']
out += L[loop:loop_end + 1]
out += ['',
        '  return Int_Glob == 5 && Bool_Glob == 1 && Ch_1_Glob == \'A\' && Ch_2_Glob == \'B\'',
        '      && Arr_1_Glob[8] == 7 && Arr_2_Glob[8][7] == Number_Of_Runs + 10',
        '      && Int_1_Loc == 5 && Int_2_Loc == 13 && Int_3_Loc == 7 && Enum_Loc == Ident_2',
        '      && strcmp (Str_1_Loc, "DHRYSTONE PROGRAM, 1\'ST STRING") == 0',
        '      && strcmp (Str_2_Loc, "DHRYSTONE PROGRAM, 2\'ND STRING") == 0;',
        '}']
out += L[main_end + 1:]
io.open('dhry_1_amiga.c', 'w', encoding='latin-1', newline='\n').write('\n'.join(out))
print('main %d..%d, loop %d..%d' % (m + 1, main_end + 1, loop + 1, loop_end + 1))
