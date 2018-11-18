/**
 * \file src/opustags.h
 * \brief Interface of all the submodules of opustags.
 */

#include <ogg/ogg.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace ot {

/**
 * Possible return status.
 *
 * The cut error family means that the end of packet was reached when attempting to read the
 * overflowing value. For example, cut_comment_count means that after reading the vendor string,
 * less than 4 bytes were left in the packet.
 */
enum class st {
	/* Generic */
	ok,
	int_overflow,
	standard_error,
	/* Ogg */
	end_of_stream,
	end_of_page,
	stream_not_ready,
	libogg_error,
	/* Opus */
	bad_magic_number,
	cut_magic_number,
	cut_vendor_length,
	cut_vendor_data,
	cut_comment_count,
	cut_comment_length,
	cut_comment_data,
	/* CLI */
	bad_arguments,
	exit_now, /**< The program should terminate successfully. */
	fatal_error,
};

/**
 * Wraps a status code with an optional message. It is implictly converted from and to a
 * #status_code.
 *
 * All the error statuses should be accompanied with a relevant error message.
 */
struct status {
	status(st code = st::ok) : code(code) {}
	template<class T> status(st code, T&& message) : code(code), message(message) {}
	operator st() { return code; }
	st code;
	std::string message;
};

/**
 * \defgroup ogg Ogg
 *
 * High-level interface for libogg.
 *
 * This module is not meant to be a complete libogg wrapper, but rather a convenient and highly
 * specialized layer above libogg and stdio.
 *
 * \{
 */

/**
 * Ogg reader, combining a FILE input, an ogg_sync_state reading the pages, and an ogg_stream_state
 * extracting the packets from the page.
 *
 * Call #read_page repeatedly until #status::end_of_stream to consume the stream, and use #page to
 * check its content. To extract its packets, call #read_packet until #status::end_of_packet.
 */
class ogg_reader {
public:
	/**
	 * Initialize the reader with the given input file handle. The caller is responsible for
	 * keeping the file handle alive, and to close it.
	 */
	ogg_reader(FILE* input);
	/**
	 * Clear all the internal memory allocated by libogg for the sync and stream state. The
	 * page and the packet are owned by these states, so nothing to do with them.
	 *
	 * The input file is not closed.
	 */
	~ogg_reader();
	/**
	 * Read the next page from the input file. The result, provided the status is #status::ok,
	 * is made available in the #page field, is owned by the Ogg reader, and is valid until the
	 * next call to #read_page.
	 *
	 * After the last page was read, return #status::end_of_stream.
	 */
	status read_page();
	/**
	 * Read the next available packet from the current #page. The packet is made available in
	 * the #packet field.
	 *
	 * No packet can be read until a page has been loaded with #read_page. If that happens,
	 * return #status::stream_not_ready.
	 *
	 * After the last packet was read, return #status::end_of_page.
	 */
	status read_packet();
	/**
	 * Current page from the sync state.
	 *
	 * Its memory is managed by libogg, inside the sync state, and is valid until the next call
	 * to ogg_sync_pageout, wrapped by #read_page.
	 */
	ogg_page page;
	/**
	 * Current packet from the stream state.
	 *
	 * Its memory is managed by libogg, inside the stream state, and is valid until the next
	 * call to ogg_stream_packetout, wrapped by #read_packet.
	 */
	ogg_packet packet;
private:
	/**
	 * The file is our source of binary data. It is not integrated to libogg, so we need to
	 * handle it ourselves.
	 *
	 * The file is not owned by the ogg_reader instance.
	 */
	FILE* file;
	/**
	 * The sync layer gets binary data and yields a sequence of pages.
	 *
	 * A page contains packets that we can extract using the #stream state, but we only do that
	 * for the headers. Once we got the OpusHead and OpusTags packets, all the following pages
	 * are simply forwarded to the Ogg writer.
	 */
	ogg_sync_state sync;
	/**
	 * Indicates whether the stream has been initialized or not.
	 *
	 * To initialize it properly, we need the serialno of the stream, which is available only
	 * after the first page was read.
	 */
	bool stream_ready = false;
	/**
	 * Indicates if the stream's last fed page is the current one.
	 *
	 * Its state is irrelevant if the stream is not ready.
	 */
	bool stream_in_sync;
	/**
	 * The stream layer receives pages and yields a sequence of packets.
	 *
	 * A single page may contain several packets, and a single packet may span on multiple
	 * pages. The 2 packets we're interested in occupy whole pages though, in theory, but we'd
	 * better ensure there are no extra packets anyway.
	 *
	 * After we've read OpusHead and OpusTags, we don't need the stream layer anymore.
	 */
	ogg_stream_state stream;
};

/**
 * An Ogg writer lets you write ogg_page objets to an output file, and assemble packets into pages.
 *
 * It has two modes of operations :
 *   1. call #write_page, or
 *   2. call #prepare_stream, then #write_packet one or more times, followed by #flush_page.
 *
 * You can switch between the two modes, but must not start writing packets and then pages without
 * flushing.
 */
class ogg_writer {
public:
	/**
	 * Initialize the writer with the given output file handle. The caller is responsible for
	 * keeping the file handle alive, and to close it.
	 */
	ogg_writer(FILE* output);
	/**
	 * Clears the stream state and any internal memory. Does not close the output file.
	 */
	~ogg_writer();
	/**
	 * Write a whole Ogg page into the output stream.
	 *
	 * This is a basic I/O operation and does not even require libogg, or the stream.
	 */
	status write_page(const ogg_page& page);
	/**
	 * Prepare the stream with the given Ogg serial number.
	 *
	 * If the stream is already configured with the right serial number, it doesn't do anything
	 * and is cheap to call.
	 *
	 * If the stream contains unflushed packets, they will be lost.
	 */
	status prepare_stream(long serialno);
	/**
	 * Add a packet to the current page under assembly.
	 *
	 * If the packet is coming from a different page, make sure the serial number fits by
	 * calling #prepare_stream.
	 *
	 * When the page is complete, you should call #flush_page to finalize the page.
	 *
	 * You must not call #write_page after it, until you call #flush_page.
	 */
	status write_packet(const ogg_packet& packet);
	/**
	 * Write the page under assembly. Future calls to #write_packet will be written in a new
	 * page.
	 */
	status flush_page();
private:
	/**
	 * The stream state receives packets and generates pages.
	 *
	 * In our specific use case, we only need it to put the OpusHead and OpusTags packets into
	 * their own pages. The other pages are naively written to the output stream.
	 */
	ogg_stream_state stream;
	/**
	 * Output file. It should be opened in binary mode. We use it to write whole pages,
	 * represented as a block of data and a length.
	 */
	FILE* file;
};

/**
 * Ogg packet with dynamically allocated data.
 *
 * Provides a wrapper around libogg's ogg_packet with RAII.
 */
struct dynamic_ogg_packet : ogg_packet {
	/** Construct an ogg_packet of the given size. */
	explicit dynamic_ogg_packet(size_t size) {
		bytes = size;
		data = std::make_unique<unsigned char[]>(size);
		packet = data.get();
	}
private:
	/** Owning reference to the data. Use the packet field from ogg_packet instead. */
	std::unique_ptr<unsigned char[]> data;
};

/** \} */

/**
 * \defgroup opus Opus
 * \brief Opus packet decoding and recoding.
 *
 * \{
 */

/**
 * Represent all the data in an OpusTags packet.
 */
struct opus_tags {
	/**
	 * OpusTags packets begin with a vendor string, meant to identify the
	 * implementation of the encoder. It is an arbitrary UTF-8 string.
	 */
	std::string vendor;
	/**
	 * Comments. These are a list of string following the NAME=Value format.
	 * A comment may also be called a field, or a tag.
	 *
	 * The field name in vorbis comment is case-insensitive and ASCII,
	 * while the value can be any valid UTF-8 string.
	 */
	std::list<std::string> comments;
	/**
	 * According to RFC 7845:
	 * > Immediately following the user comment list, the comment header MAY contain
	 * > zero-padding or other binary data that is not specified here.
	 *
	 * The first byte is supposed to indicate whether this data should be kept or not, but let's
	 * assume it's here for a reason and always keep it. Better safe than sorry.
	 *
	 * In the future, we could add options to manipulate this data: view it, edit it, truncate
	 * it if it's marked as padding, truncate it unconditionally.
	 */
	std::string extra_data;
};

/**
 * Validate the content of the first packet of an Ogg stream to ensure it's a valid OpusHead.
 *
 * Returns #ot::status::ok on success, #ot::status::bad_identification_header on error.
 */
status validate_identification_header(const ogg_packet& packet);

/**
 * Read the given OpusTags packet and extract its content into an opus_tags object.
 *
 * On error, the tags object is left unchanged.
 */
status parse_tags(const ogg_packet& packet, opus_tags& tags);

/**
 * Serialize an #opus_tags object into an OpusTags Ogg packet.
 */
dynamic_ogg_packet render_tags(const opus_tags& tags);

/**
 * Remove all the comments whose field name is equal to the special one, case-sensitive.
 */
void delete_comments(opus_tags& tags, const char* field_name);

/** \} */

/**
 * \defgroup cli Command-Line Interface
 * \{
 */

struct options {
	std::string path_in;
	std::string path_out;
	/**
	 * If null, in-place editing is disabled.
	 * Otherwise, it contains the suffix to add to the file name.
	 */
	const char *inplace = nullptr;
	std::vector<std::string> to_add;
	std::vector<std::string> to_delete;
	bool delete_all = false;
	bool set_all = false;
	bool overwrite = false;
	bool print_help = false;
};

status process_options(int argc, char** argv, options& opt);

void print_comments(const std::list<std::string>& comments, FILE* output);
std::list<std::string> read_comments(FILE* input);

status run(options& opt);
status process(ogg_reader& reader, ogg_writer* writer, const options &opt);

/** \} */

}
