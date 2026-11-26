use strict;
use warnings;
use JSON::PP qw(decode_json encode_json);

$| = 1;
while (my $line = <STDIN>) {
    my $request = decode_json($line);
    print encode_json({
        type => 'PLAY',
        request_id => $request->{request_id},
        commands => ['HARD_DROP'],
    }), "\n";
}
