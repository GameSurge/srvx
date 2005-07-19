#! /usr/bin/perl -w

use strict;
use warnings;
use vars qw($field_re %lang %escapes);
use FileHandle ();

$| = 1;

$field_re = qr/%.*?[diouxXeEfFgGaAcspn%]/;

%escapes = (
            '"' => '"',
            'n' => "\n",
            '\\' => "\\"
           );

sub split_format ($$$) {
  my ($language, $key, $format) = @_;
  my (@fields, @sorted, $indexed, $idx);

  # C indexes things from argument 1.
  $fields[0] = { type => 'dummy' };

  # Parse each format field in the string.
  while ($format =~ /($field_re)/g) {
    my $field = $1;
    next if substr($field, -1) eq '%';
    if ($field =~ /^%(\d+\$)(#?0?-? ?\+?)(\*\d+\$|\d*)(.\*\d+\$|\.\d+)?((?:hh?|ll?|L|j|z|t)?.)$/) {
      if (not defined $indexed) {
        $indexed = 1;
      } elsif (not $indexed) {
        print "MISMATCH: ${language} ${key}, mix of indexed and unindexed fields\n";
        return ();
      }
      my $res = {};
      $res->{index} = substr($1, 0, -1);
      $res->{flags} = $2;
      $res->{width} = $3;
      $res->{precision} = $4;
      $res->{type} = $5;
      $res->{width_idx} = $1
        if $res->{width} and $res->{width} =~ /^\*\d+\$$/;
      $res->{prec_idx} = $1
        if $res->{precision} and $res->{precision} =~ /^.\*(\d+)\$$/;
      push @fields, $res;
    } elsif ($field =~ /^%(#?0?-? ?\+?)(\*|\d*)(.\*|\.\d+)?((?:hh?|ll?|L|j|z|t)?.)$/) {
      if (not defined $indexed) {
        $indexed = 0;
        $idx = 1;
      } elsif ($indexed) {
        print "MISMATCH: ${language} ${key}, mix of indexed and unindexed fields\n";
        return ();
      }
      my $res = {};
      $res->{flags} = $1;
      $res->{width} = $2;
      $res->{precision} = $3;
      $res->{type} = $4;
      $res->{width_idx} = $idx++
        if $res->{width} and $res->{width} eq '*';
      $res->{prec_idx} = $idx++
        if $res->{precision} and $res->{precision} eq '.*';
      $res->{index} = $idx++;
      push @fields, $res;
    } else {
      print "Unparsed field ${language} ${key}: $field\n";
      next;
    }
  }

  # Go through and make sure they are in fully sorted order, with
  # precision arguments marked properly.
  foreach my $field (@fields) {
    next if $field->{type} eq 'dummy' or $field->{type} eq 'width' or $field->{type} eq 'precision';
    my $idx = $field->{index};

    # Check for conflicts with this field.
    if (my $old = $sorted[$idx]) {
      if ($old->{type} ne $field->{type}) {
        print "MISMATCH ${key}: ${language} refers to param $idx as both type ".$old->{type}." and ".$field->{type}.".\n";
        next;
      }
      if ($old->{precision} or $field->{precision}) {
        if (exists($old->{prec_idx}) != exists($field->{prec_idx})) {
          print "MISMATCH ${key}: ${language} has param $idx with and without a precision argument.\n";
          next;
        } elsif ($old->{prec_idx} != $field->{prec_idx}) {
          print "MISMATCH ${key}: ${language} has param $idx with different precision arguments.\n";
          next;
        }
      }
      if ($old->{width} or $field->{width}) {
        if (exists($old->{width_idx}) != exists($field->{width_idx})) {
          print "MISMATCH ${key}: ${language} has param $idx with and without width argument.\n";
        } elsif ($old->{width_idx} != $field->{width_idx}) {
          print "MISMATCH ${key}: ${language} has param $idx with different width arguments.\n";
        }
      }
    }
    $sorted[$idx] = $field;

    if (exists($field->{width_idx})) {
      my $width_idx = $field->{width_idx};
      if (my $old = $sorted[$width_idx]) {
        if ($old->{type} ne 'width') {
          print "MISMATCH ${key}: ${language} refers to param $idx as both type ".$old->{type}." and type width.\n";
          next;
        }
      }
      $sorted[$width_idx] = { type => 'width' };
    }

    if (exists($field->{prec_idx})) {
      my $prec_idx = $field->{prec_idx};
      # Check for conflicts with this field's precision argument.
      if (my $old = $sorted[$prec_idx]) {
        if ($old->{type} ne 'precision') {
          print "MISMATCH ${key}: ${language} refers to param $idx as both type ".$old->{type}." and type precision.\n";
          next;
        }
      }
      $sorted[$prec_idx] = { type => 'precision' };
    }
  }

  return @sorted;
}

sub compare_formats ($$$$) {
  my ($language, $key, $orig_fmt, $new_fmt) = @_;

  my @orig_fields = split_format('C', $key, $orig_fmt);
  my @new_fields = split_format($language, $key, $new_fmt);
  if (scalar(@orig_fields) != scalar(@new_fields)) {
    print "MISMATCH ${key}: C has ".scalar(@orig_fields)." fields, ${language} has ".scalar(@new_fields)."\n";
    return;
  }
  for (my $x = 1; $x <= $#orig_fields; $x++) {
    my $orig = $orig_fields[$x];
    my $new = $new_fields[$x];
    if (not exists $orig->{type}) {
      print "MISMATCH ${key}: C has no type for format $x!\n";
    } elsif (not exists $new->{type}) {
      print "MISMATCH ${key}: ${language} has no type for format $x!\n";
    } if ($orig->{type} ne $new->{type}) {
      print "MISMATCH ${key}: C refers to argument $x as type ".$orig->{type}.", ${language} as type ".$new->{type}.".\n";
      next;
    }
    if ($orig->{width} or $new->{width}) {
      if (not exists ($orig->{width_idx}) and not exists($new->{width_idx})) {
        # both used fixed widths: no problem
      } elsif (exists($orig->{width_idx}) and not exists($new->{width_idx})) {
        print "MISMATCH ${key}: C has a width argument for format $x, ${language} does not.\n";
      } elsif (not exists($orig->{width_idx}) and exists($new->{width_idx})) {
        print "MISMATCH ${key}: ${language} has a width argument for format $x, C does not.\n";
      } elsif ($orig->{width_idx} != $new->{width_idx}) {
        print "MISMATCH ${key}: C and ${language} disagree on width argument for format $x.\n";
      }
    }
    if ($orig->{precision} or $new->{precision}) {
      if (not exists($orig->{prec_idx}) and not exists($new->{prec_idx})) {
        # both used fixed precisions: no problem
      } elsif (exists($orig->{prec_idx}) and not exists($new->{prec_idx})) {
        print "MISMATCH ${key}: C has a precision argument for format $x, ${language} does not.\n";
        next;
      } elsif (not exists($orig->{prec_idx}) and exists($new->{prec_idx})) {
        print "MISMATCH ${key}: $language has a precision argument for format $x, C does not.\n";
        next;
      } elsif ($orig->{prec_idx} != $new->{prec_idx}) {
        print "MISMATCH ${key}: C and $language disagree on precision argument for format $x.\n";
        next;
      }
    }
  }
}

sub read_language ($) {
  my $fname = shift;
  my $fh = new FileHandle($fname, "r");
  return undef unless defined $fh;
  my $res = {};
  while (defined($_ = $fh->getline)) {
    chomp;
    if (my ($key, $val) = /^"(\w+)" "(.+)";$/) {
      $val =~ s/\\(.)/$escapes{$1}/eg;
      $res->{$key} = $val;
    } else {
      print "Unrecognized line in $fname: $_\n";
    }
  }
  return $res;
}

$lang{C} = read_language("strings.db");

foreach my $language (@ARGV) {
  next if exists $lang{$language};
  $lang{$language} = read_language("${language}/strings.db")
    or die "Unable to read $language: $!";
  foreach my $key (keys %{$lang{$language}}) {
    if (not $lang{C}->{$key}) {
      print "Extra entry in ${language}: $key\n";
      next;
    }
    compare_formats($language, $key, $lang{C}->{$key}, $lang{$language}->{$key});
  }
}
