#include <opustags.h>
#include "tap.h"

#include <string.h>

using namespace std::literals::string_literals;

static void check_identification()
{
	ogg_packet packet {};
	packet.packet = (unsigned char*) "OpusHead..";
	packet.bytes = 10;
	if (ot::validate_identification_header(packet) != ot::st::ok)
		throw failure("did not accept a good OpusHead");

	packet.bytes = 7;
	if (ot::validate_identification_header(packet) != ot::st::cut_magic_number)
		throw failure("accepted an OpusHead that is too short");

	packet.packet = (unsigned char*) "NotOpusHead";
	packet.bytes = 11;
	if (ot::validate_identification_header(packet) != ot::st::bad_magic_number)
		throw failure("did not report the right status for a bad OpusHead");
}

static const char standard_OpusTags[] =
	"OpusTags"
	"\x14\x00\x00\x00" "opustags test packet"
	"\x02\x00\x00\x00"
	"\x09\x00\x00\x00" "TITLE=Foo"
	"\x0a\x00\x00\x00" "ARTIST=Bar";

static void parse_standard()
{
	ot::opus_tags tags;
	ogg_packet op;
	op.bytes = sizeof(standard_OpusTags) - 1;
	op.packet = (unsigned char*) standard_OpusTags;
	auto rc = ot::parse_tags(op, tags);
	if (rc != ot::st::ok)
		throw failure("ot::parse_tags did not return ok");
	if (tags.vendor != "opustags test packet")
		throw failure("bad vendor string");
	if (tags.comments.size() != 2)
		throw failure("bad number of comments");
	auto it = tags.comments.begin();
	if (*it != "TITLE=Foo")
		throw failure("bad title");
	++it;
	if (*it != "ARTIST=Bar")
		throw failure("bad artist");
	if (tags.extra_data.size() != 0)
		throw failure("found mysterious padding data");
}

/**
 * Try parse_tags with packets that should not valid, or that might even
 * corrupt the memory. Run this one with valgrind to ensure we're not
 * overflowing.
 */
static void parse_corrupted()
{
	size_t size = sizeof(standard_OpusTags);
	char packet[size];
	memcpy(packet, standard_OpusTags, size);
	ot::opus_tags tags;
	ogg_packet op;
	op.packet = (unsigned char*) packet;
	op.bytes = size;

	char* header_data = packet;
	char* vendor_length = header_data + 8;
	char* vendor_string = vendor_length + 4;
	char* comment_count = vendor_string + *vendor_length;
	char* first_comment_length = comment_count + 4;
	char* first_comment_data = first_comment_length + 4;
	char* end = packet + size;

	op.bytes = 7;
	if (ot::parse_tags(op, tags) != ot::st::cut_magic_number)
		throw failure("did not detect the overflowing magic number");
	op.bytes = 11;
	if (ot::parse_tags(op, tags) != ot::st::cut_vendor_length)
		throw failure("did not detect the overflowing vendor string length");
	op.bytes = size;

	header_data[0] = 'o';
	if (ot::parse_tags(op, tags) != ot::st::bad_magic_number)
		throw failure("did not detect the bad magic number");
	header_data[0] = 'O';

	*vendor_length = end - vendor_string + 1;
	if (ot::parse_tags(op, tags) != ot::st::cut_vendor_data)
		throw failure("did not detect the overflowing vendor string");
	*vendor_length = end - vendor_string - 3;
	if (ot::parse_tags(op, tags) != ot::st::cut_comment_count)
		throw failure("did not detect the overflowing comment count");
	*vendor_length = comment_count - vendor_string;

	++*comment_count;
	if (ot::parse_tags(op, tags) != ot::st::cut_comment_length)
		throw failure("did not detect the overflowing comment length");
	*first_comment_length = end - first_comment_data + 1;
	if (ot::parse_tags(op, tags) != ot::st::cut_comment_data)
		throw failure("did not detect the overflowing comment data");
}

static void recode_standard()
{
	ot::opus_tags tags;
	ogg_packet op;
	op.bytes = sizeof(standard_OpusTags) - 1;
	op.packet = (unsigned char*) standard_OpusTags;
	auto rc = ot::parse_tags(op, tags);
	if (rc != ot::st::ok)
		throw failure("ot::parse_tags did not return ok");
	auto packet = ot::render_tags(tags);
	if (packet.b_o_s != 0)
		throw failure("b_o_s should not be set");
	if (packet.e_o_s != 0)
		throw failure("e_o_s should not be set");
	if (packet.granulepos != 0)
		throw failure("granule_post should be 0");
	if (packet.packetno != 1)
		throw failure("packetno should be 1");
	if (packet.bytes != sizeof(standard_OpusTags) - 1)
		throw failure("the packet is not the right size");
	if (memcmp(packet.packet, standard_OpusTags, packet.bytes) != 0)
		throw failure("the rendered packet is not what we expected");
}

static void recode_padding()
{
	ot::opus_tags tags;
	std::string padded_OpusTags(standard_OpusTags, sizeof(standard_OpusTags));
	// ^ note: padded_OpusTags ends with a null byte here
	padded_OpusTags += "hello";
	ogg_packet op;
	op.bytes = padded_OpusTags.size();
	op.packet = (unsigned char*) padded_OpusTags.data();

	auto rc = ot::parse_tags(op, tags);
	if (rc != ot::st::ok)
		throw failure("ot::parse_tags did not return ok");
	if (tags.extra_data != "\0hello"s)
		throw failure("corrupted extra data");
	// recode the packet and ensure it's exactly the same
	auto packet = ot::render_tags(tags);
	if (static_cast<size_t>(packet.bytes) < padded_OpusTags.size())
		throw failure("the packet was truncated");
	if (static_cast<size_t>(packet.bytes) > padded_OpusTags.size())
		throw failure("the packet got too big");
	if (memcmp(packet.packet, padded_OpusTags.data(), packet.bytes) != 0)
		throw failure("the rendered packet is not what we expected");
}

int main()
{
	std::cout << "1..5\n";
	run(check_identification, "check the OpusHead packet");
	run(parse_standard, "parse a standard OpusTags packet");
	run(parse_corrupted, "correctly reject invalid packets");
	run(recode_standard, "recode a standard OpusTags packet");
	run(recode_padding, "recode a OpusTags packet with padding");
	return 0;
}
