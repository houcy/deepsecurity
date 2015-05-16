##! Utilities specific for HTTP processing.

@load ./main
@load base/utils/addrs

module HTTP;

export {
	## Given a string containing a series of key-value pairs separated
	## by "=", this function can be used to parse out all of the key names.
	##
	## data: The raw data, such as a URL or cookie value.
	##
	## kv_splitter: A regular expression representing the separator between
	##              key-value pairs.
	##
	## Returns: A vector of strings containing the keys.
	global extract_keys: function(data: string, kv_splitter: pattern): string_vec;
	
	## Creates a URL from an :bro:type:`HTTP::Info` record.  This should
	## handle edge cases such as proxied requests appropriately.
	##
	## rec: An :bro:type:`HTTP::Info` record.
	##
	## Returns: A URL, not prefixed by ``"http://"``.
	global build_url: function(rec: Info): string;
	
	## Creates a URL from an :bro:type:`HTTP::Info` record.  This should
	## handle edge cases such as proxied requests appropriately.
	##
	## rec: An :bro:type:`HTTP::Info` record.
	##
	## Returns: A URL prefixed with ``"http://"``.
	global build_url_http: function(rec: Info): string;

	## Create an extremely shortened representation of a log line.
	global describe: function(rec: Info): string;
}


function extract_keys(data: string, kv_splitter: pattern): string_vec
	{
	local key_vec: vector of string = vector();
	
	local parts = split(data, kv_splitter);
	for ( part_index in parts )
		{
		local key_val = split1(parts[part_index], /=/);
		if ( 1 in key_val )
			key_vec[|key_vec|] = key_val[1];
		}
	return key_vec;
	}

function build_url(rec: Info): string
	{
	local uri  = rec?$uri ? rec$uri : "/<missed_request>";
        if (/^\// !in uri)
                {
		local index:count = strstr(uri, "://");
		if (index != 0)
			{
			local tmps = str_split(uri, vector(index+2));
			uri = tmps[2];
			}
		local parts = split1(uri, /\//);
		if (|parts| == 2)
                        uri =  "/" + parts[2];
		else
			uri = "/";
                }
	local host = rec?$host ? rec$host : addr_to_uri(rec$id$resp_h);
	if ( rec$id$resp_p != 80/tcp )
		host = fmt("%s:%d", host,port_to_count(rec$id$resp_p));
	return fmt("%s%s", host, uri);
	}
	
function build_url_http(rec: Info): string
	{
	return fmt("http://%s", build_url(rec));
	}

function describe(rec: Info): string
	{
	return build_url_http(rec);
	}
