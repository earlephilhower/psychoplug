#!/usr/bin/perl


@files = ( "africa", "antarctica", "asia", "australasia", "europe", "northamerica", "southamerica", "pacificnew", "etcetera", "backward" );

my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime(time);
$curyear = $year + 1900;

$month{'Jan'} = "MON_JAN";
$month{'Feb'} = "MON_FEB";
$month{'Mar'} = "MON_MAR";
$month{'Apr'} = "MON_APR";
$month{'May'} = "MON_MAY";
$month{'Jun'} = "MON_JUN";
$month{'Jul'} = "MON_JUL";
$month{'Aug'} = "MON_AUG";
$month{'Sep'} = "MON_SEP";
$month{'Oct'} = "MON_OCT";
$month{'Nov'} = "MON_NOV";
$month{'Dec'} = "MON_DEC";

# If we don't do these manually, we'll get random integers per month. :(
print "#define MON_JAN (0)\n";
print "#define MON_FEB (1)\n";
print "#define MON_MAR (2)\n";
print "#define MON_APR (3)\n";
print "#define MON_MAY (4)\n";
print "#define MON_JUN (5)\n";
print "#define MON_JUL (6)\n";
print "#define MON_AUG (7)\n";
print "#define MON_SEP (8)\n";
print "#define MON_OCT (9)\n";
print "#define MON_NOV (10)\n";
print "#define MON_DEC (11)\n";

$daytrig{'Sun>='} = "SUN_GTEQ";
$daytrig{'Mon>='} = "MON_GTEQ";
$daytrig{'Tue>='} = "TUE_GTEQ";
$daytrig{'Wed>='} = "WED_GTEQ";
$daytrig{'Thu>='} = "THU_GTEQ";
$daytrig{'Fri>='} = "FRI_GTEQ";
$daytrig{'Sat>='} = "SAT_GTEQ";
$daytrig{'lastSun'} = "SUN_LAST";
$daytrig{'lastMon'} = "MON_LAST";
$daytrig{'lastTue'} = "TUE_LAST";
$daytrig{'lastWed'} = "WED_LAST";
$daytrig{'lastThu'} = "THU_LAST";
$daytrig{'lastFri'} = "FRI_LAST";
$daytrig{'lastSat'} = "SAT_LAST";
$daytrig{'givenDay'} = "GIVEN_DAY";

$atref{'local'} = "ATREF_W"; # Wall clock (subject to daylight savings)
$atref{'u'} = "ATREF_U";     # UTC/GMT time
$atref{'s'} = "ATREF_S";     # Local standard w/o taking daylight savings into account

#$i=0; foreach $k (keys %month) { print "#define $month{$k} ($i)\n"; $i++ }
$i=0; foreach $k (keys %daytrig) { print "#define $daytrig{$k} ($i)\n"; $i++ }
$i=0; foreach $k (keys %atref) { print "#define $atref{$k} ($i)\n"; $i++ }


$rulenames{'RULE_NONE'} = 1;

foreach $f (@files) {
	open F, "<$f";
	while ($line = <F>) {
		chomp $line;
		$line =~ s/#.*//;
		@w = split(/\s+/, $line);
		if (($w[0] eq "Rule") and ((($w[3] eq "max") and ($w[2]<=$curyear)) or (($w[3] eq "only")and ($w[2]==$curyear))) ) {
			$w[1] =~ s/-/_/g;
			$name = "RULE_" . $w[1];
			next if (($name eq "RULE_Morocco") and ($w[2] eq "2013")); # Contradictory info in the Morocco entry, shows a MAX in 2013 and then other entries
			$mon = $month{$w[5]};
			$day = $w[6];
			($day, $daynum) = split(/\=/, $day);
			if ($day =~ /^[0-9]+$/) {
				$daynum = $day;
				$day = "givenDay";
				$day = $daytrig{$day};
			} else {
				$day = $day . "=" if ($daynum ne "");
				$day = $daytrig{$day};
	                        $daynum = "0" if ($daynum eq "");
			}
			$time = $w[7];
                        if ($time =~ /u$/) {$atus = $atref{'u'}; $time =~ s/u$//; }
                        elsif ($time =~ /s$/) {$atus = $atref{'s'}; $time =~ s/s$//; }
			else { $atus = $atref{'local'}; }
			($ath, $atm) = split(/:/, $time);
			if ($ath == 24) { $ath = $23; $atm = 59; }
			$ath = int($ath);
			$atm = int($atm);
			$delta = $w[8];
			($deltah, $deltam) = split(/:/, $delta);
			if ($deltah == 24) { $deltah = $23; $deltam = 59; }
			$deltah = int($deltah);
			$deltam = int($deltam);
			$fmtstr = $w[9];
			$fmtstr = "" if ($fmtstr eq "-");
			$rulenames{$name} = $w[3];
			push @rule, "{$name, $mon, $day, $daynum, $ath, $atm, $atus, $deltah, $deltam, \"$fmtstr\"},\n";
		}
	}
	close F
}

$i=0; foreach $k (keys %rulenames) { print "#define $k ($i)\n"; $i++ }
print <<EOF

typedef struct {
       uint8_t name       : 6; // Empirical
       uint8_t month      : 4; // 0...11
       uint8_t daytrig    : 4; // 0...14
       uint8_t daynum     : 5; // 0...31
       uint8_t athr       : 4; // 0..11 
       uint8_t atmin      : 6; // 0..59
       uint8_t atref      : 2; // 0..2
       uint8_t offsethrs  : 2; // 0..2
       uint8_t offsetmins : 6; // 0..59
       char *fmtstr;           // Empirical, longest is >> average
} rule_t;
EOF
;
@rule = sort @rule;
print("static const rule_t rules[] = {\n");
foreach $k (@rule) {
	print "$k"
}
print "};\n";


foreach $f (@files) {
	open F, "<$f";
	while ($line = <F>) {
		chomp $line;
		$line =~ s/#.*//;
		@w = split(/\s+/, $line);
		if ($w[0] eq "Zone") {
			$zonename = $w[1];
			$gmtoff = $w[2];
			$rules = $w[3];
			$fmt = $w[4];
			$until = $w[5];
		} elsif (($zonename ne "") && ($w[1] ne "")) {
			$gmtoff = $w[1];
			$rules = $w[2];
			$fmt = $w[3];
			$until = $w[4];
		}
		if (($zonename ne "") && (($until eq "") || ($until >= $curyear)) ) {
			$rules = "NONE" if ($rules eq "-");
			$rules =~ s/-/_/g;
			$rules = "RULE_" . $rules;
			$rules = "RULE_NONE" if (! exists($rulenames{$rules})); # They're not using DST anymore
			($h, $m) = split(/:/, $gmtoff);
			$h = 0 if ($h eq "");
			$m = 0 if ($m eq "");
			$h = int($h);
			$m = int($m);
			push @zone, "{REPLACEME, /*$zonename*/ \"$zonename\", $h, $m, $rules, \"$fmt\"},\n";
			$zonename= "";
		}
	}
	close F
}
@zone = sort @zone;

foreach $f (@files) {
        open F, "<$f";
         while ($line = <F>) {
                chomp $line;
                $line =~ s/#.*//;
                @w = split(/\s+/, $line);
                if ($w[0] eq "Link") {
			$destzone = $w[1];
			$linkname = $w[2];
			for ($i = 0; $i < @zone; $i++) {
				$thisname = $zone[$i];
				$thisname =~ s/\"(.*)?\",//;
				$thisname = $1;
				if ($thisname eq $destzone) {
					push @link, "{ REPLACEME, \"$linkname\", $i /*$thisname*/ },\n";
					break;
				}
			}
		}
	}
	close F;
}
@link = sort @link;


print <<EOF
typedef struct {
	uint8_t zoneNameFromPrev : 5; // 0..31
	char *zonename;
	int8_t gmtoffhr   : 5; // -12...+12
	uint8_t gmtoffmin : 6; // 0..59
	uint8_t rule      : 6; // Empirical 44 rules
	char *formatstr;       // Empirical lots of variation
} timezone_t;

EOF
;


print "static const timezone_t mtimezone[] = {\n";

# Poor man's RLE.  Find # of matching chars from previous name and omit them
$prevName = "";
foreach $z (@zone)
{
	$thisname = $z;
	$thisname =~ s/\"(.*)?\",//;
	$thisname = $1;
	$t = $thisname;
	$p = $prevname;
	$cnt = 0;
	while (substr($p,$cnt,1) eq substr($t,$cnt,1)) {$cnt++;}
        $cnt = 31 if ($cnt > 31); # 5-bits wide
	$delta = substr($thisname, $cnt);
	$z =~ s/REPLACEME/$cnt/;
	$z =~ s/\".*?\",/\"$delta\",/;
	print $z;
	$prevname = $thisname;
}
print "};\n";

print <<EOF
typedef struct {
	uint8_t zoneNameFromPrev : 5; // 0..31
	char *zonename;
	uint16_t timezone        : 9; // Empirical # of timezones < 511
} link_t;

EOF
;

print "static const link_t link[] = {\n";
# Poor man's RLE.  Find # of matching chars from previous name and omit them
$prevName = "";
foreach $z (@link)
{
        $thisname = $z;
        $thisname =~ s/\"(.*)?\"//;
        $thisname = $1;
        $t = $thisname;
        $p = $prevname;
        $cnt = 0;
        while (substr($p,$cnt,1) eq substr($t,$cnt,1)) {$cnt++;}
        $cnt = 31 if ($cnt > 31); # 5-bits wide
        $delta = substr($thisname, $cnt);
        $z =~ s/REPLACEME/$cnt/;
        $z =~ s/\".*?\"/\"$delta\"/;
        print $z;
        $prevname = $thisname;
}


print "};\n"
