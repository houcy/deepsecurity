#include "Precompiled.h"
#include "WebService.h"
#include "sha1.h"
#include "StringHelper.h"
#include <algorithm>

signal_CMDEvent signalcmdEvent;

int	serverSocket = 0;
DWORD dwTlsIndex = 0;
HANDLE handle_mutex;
struct http_message

{

	unsigned char method;

	char request_uri[MAX_URI_SIZE];

	char *post_data;

	DWORD post_data_len;

	int status;

};

int callback_uri(http_parser *parser, const char *pos, size_t len)

{

	http_message *message = (http_message *) TlsGetValue(dwTlsIndex);

	char uri_buf[MAX_URI_SIZE];

	DWORD uri_buflen = MAX_URI_SIZE;

	

	strncpy_s(uri_buf, MAX_URI_SIZE, pos, len);

	InternetCanonicalizeUrlA(uri_buf, message->request_uri, &uri_buflen, ICU_DECODE | ICU_NO_ENCODE);



	return 0;

}


int callback_body(http_parser *parser, const char *pos, size_t len)

{

	http_message *message = (http_message *) TlsGetValue(dwTlsIndex);

	

	message->post_data = (char *) (pos+len);

	message->post_data_len += len;

//	LOG(INFO,"HTTP message POST (%d bytes) %p\n", message->post_data_len, message->post_data);



	return 0;

}



int callback_complete(http_parser *parser)

{
	http_message *message = (http_message *) TlsGetValue(dwTlsIndex);

	message->status = WEBSVC_HTTP_SUCCESS;
	message->method = parser->method;

	return 0;
}



int serve_error(int *clientSocket, char *status)

{
	char buf[SERVER_BUFLEN];
	int buflen = SERVER_BUFLEN;

	sprintf_s(buf, buflen, "HTTP/1.0 %s\r\n\r\n%s", status, status);

	return send(*clientSocket, buf, strlen(buf), 0);
}



int serve_file(int *clientSocket, http_message *message, const char *filepath)

{
	// Check if file exists
	DWORD dwAttrib = GetFileAttributesA(filepath);
	BOOL isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

	if (isFile)
	{

		HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);

		if (hFile != INVALID_HANDLE_VALUE)

		{

			DWORD filesize_high, filesize = GetFileSize(hFile, &filesize_high);



			if (filesize > MAX_FILE_SIZE)

			{

				CloseHandle(hFile);



				return serve_error(clientSocket, "403 Forbidden (file too large)");

			}

			else

			{

				int bytecount = 0;

				DWORD bytecount_file = 0;



				// Send HTTP header

				const char http_header[] =	"HTTP/1.0 200 OK\r\n"

											"Content-Transfer-Encoding: binary\r\n"

											"Content-Type: application/octet-stream\r\n"

											"\r\n";

											// TODO

											// "Content-Length: 3495\r\n"

											// "Content-Disposition: attachment; filename=123.bin\r\n"

				bytecount = send(*clientSocket, http_header, strlen(http_header), 0);



				// Allocate buffer and read file

				char *buf = (char *) malloc(filesize);

				if(!ReadFile(hFile, buf, filesize, &bytecount_file, NULL))

				{

					CloseHandle(hFile);

					free(buf);

					return serve_error(clientSocket, "403 Forbidden (file read error)");

				}

				CloseHandle(hFile);

				

				// Send file and free buffer

				bytecount += send(*clientSocket, buf, bytecount_file, 0);

				free(buf);



				return bytecount;

			}

		}

		else

			return serve_error(clientSocket, "403 Forbidden (file open error)");

	}

	else

		return serve_error(clientSocket, "404 Not found");

}



int write_hash_to_log(int *clientSocket,http_message *message,char *filepath)

{
	SHA1Context sha;
	SHA1Reset(&sha);
	SHA1Input(&sha, (const unsigned char *)message->post_data, message->post_data_len);

	if (!SHA1Result(&sha))

	{
		printf("Error in calculating SHA1!\n");
		return serve_error(clientSocket, "403 calculating sha1 error!");
	}

	else
	{				
		FILE *fp;
		if((fp=fopen(FILE_DATA_POLLUTION,"a"))==NULL)

		{
			printf("open file error! \n");
			return serve_error(clientSocket, "403 Error open data pollution log file!");
		}

		//calculating the time, and write it to log file

		time_t ltime;
		time( &ltime );
		fprintf(fp,"[upload time]%ld\t",ltime );

		// write file hash to log file	

		fprintf(fp,"[file sha1]%08X%08X%08X%08X%08X\t",

			sha.Message_Digest[0],

			sha.Message_Digest[1],

			sha.Message_Digest[2],

			sha.Message_Digest[3],

			sha.Message_Digest[4]);



		//write file path to log file

		fprintf(fp,"[file path]%s\n",filepath);
		fclose(fp);
	}
}

int recv_file(int *clientSocket, http_message *message, const char *filepath)

{
	int bytecount = 0;
	if (message->post_data && message->post_data_len)

	{

		//get mutex

/*		WaitForSingleObject(handle_mutex,INFINITE);

		// Check if data pollutin log file exists

		DWORD dwAttrib = GetFileAttributesA(FILE_DATA_POLLUTION);

		BOOL isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));



		//tricky here, first write it to the data pollution log file, then if it is the second upload, do not write it to disk

		write_hash_to_log(clientSocket,message,filepath);

		

		ReleaseMutex(handle_mutex);
*/


//		if (!isFile)

		{
			char file[1024]={0};
			sprintf(file, "%s%s",TEMP_FILE_FOLDER, filepath);
			
			HANDLE hFile = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

			if (hFile != INVALID_HANDLE_VALUE)

			{

				DWORD bytecount_file = 0;



				// Write POST contents to file

				if (!WriteFile(hFile, message->post_data, message->post_data_len, &bytecount_file, 0))

				{

					CloseHandle(hFile);

					//LOG(WARN,"Could not write file %s from buffer %p (%d)\n", filepath, message->post_data, GetLastError());

					return serve_error(clientSocket, "403 Forbidden (file write error)");

				}

				if (bytecount_file != message->post_data_len)

				{

					CloseHandle(hFile);

					//LOG(WARN,"Could not write file %s (%d)\n", filepath, GetLastError());

					return serve_error(clientSocket, "403 Forbidden (file write error)");

				}



				// Send OK response

				const char http_response[] = "HTTP/1.0 201 Created\r\n\r\n";

				bytecount = send(*clientSocket, http_response, strlen(http_response), 0);



				CloseHandle(hFile);

			}

			else

			{

				//LOG(WARN,"Could not create file %s (%d)\n", filepath, GetLastError());

				return serve_error(clientSocket, "403 Forbidden (file open error)");

			}
/*
			char path_buffer[_MAX_PATH];
			char drive[_MAX_DRIVE];
			char dir[_MAX_DIR];
			char fname[_MAX_FNAME];
			char ext[_MAX_EXT];
			 _splitpath( file, drive, dir, fname, ext );
			std::string fileext(ext);
			transform(fileext.begin(), fileext.end(), fileext.begin(), ::tolower); 

			std::wstring wsfile = StringHelper::multiByteStringToWideString(file, strlen(file)+1);
			TCHAR cmdline[1024]={0};

			LPCWSTR pszImageName=NULL;

			if(fileext.compare(".exe") == 0)
			{
				pszImageName = wsfile.c_str();
			}
			else if(fileext.compare(".doc") == 0)
			{
				return 0;
			}
			else if(fileext.compare(".pdf") == 0)
			{
				swprintf(cmdline, _T("C:\\Program Files\\Adobe\\Reader 11.0\\Reader\\AcroRd32.exe %s"), wsfile.c_str());
			}
			else
				return 0;


			PROCESS_INFORMATION ProcessInfo;
			STARTUPINFO StartupInfo; //This is an [in] parameter
			ZeroMemory(&StartupInfo, sizeof(StartupInfo));
			StartupInfo.cb = sizeof StartupInfo ; //Only compulsory field
			if(CreateProcess(pszImageName, cmdline,
				 NULL,NULL,FALSE,0,NULL,
				 NULL,&StartupInfo,&ProcessInfo))
			{
				 CloseHandle(ProcessInfo.hThread);
				 CloseHandle(ProcessInfo.hProcess);
			}  
			else
			{
				std::cout<< "could not spawn " << wsfile.c_str();
			}

			Sleep(20000);


			char cmdbuf[200]={0};
			sprintf(cmdbuf,"/F /PID %d",ProcessInfo.dwProcessId);
			ShellExecuteA(0, "open", "taskkill", cmdbuf, 0, SW_HIDE);	*/		

		}

/*		else

		{

			//this is the second file recieved, meaning data pollution happened!

			return serve_error(clientSocket, "405 Forbidden (this is the second file recieved)");

		}
*/
	}

	else

		return serve_error(clientSocket, "400 Bad Request (no file received)");




	return bytecount;

}



/*

int serve_sigcheck(int *clientSocket, http_message *message, char *filepath)

{

	int result = isFileDigitallySigned(filepath);



	if (result < 0)

		return serve_error(clientSocket, "403 Forbidden (file error)");

	else

	{

		if (result == SIGCHECK_VALID)

		{

			//LOG(INFO,"Sigcheck valid signature in file %s\n", filepath);



			const char http_response[] = "HTTP/1.0 200 OK\r\n\r\n";

			return send(*clientSocket, http_response, strlen(http_response), 0);

		}

		else if (result == SIGCHECK_INVALID)

		{

			//LOG(INFO,"Sigcheck invalid signature in file %s\n", filepath);



			return serve_error(clientSocket, "409 Conflict");

		}

		else

		{

			//LOG(INFO,"Sigcheck no signature in file %s\n", filepath);



			return serve_error(clientSocket, "404 Not found");

		}

	}

}

*/

int serve_ping(int *clientSocket, http_message *message)

{

	unsigned long ping_addr = inet_addr("8.8.8.8");

	DWORD retval = InternetCheckPing(ping_addr);



	if (retval)

	{

		// Send OK response

		const char http_response[] = "HTTP/1.0 200 OK\r\n\r\n";

		return send(*clientSocket, http_response, strlen(http_response), 0);

	}

	else

	{

		return serve_error(clientSocket, "404 Not found");

	}

}


/*
int serve_explorer(int *clientSocket, http_message *message)

{

//	BOOL retval = loadGuardDLL();

	BOOL retval = OpenFile();

	if (retval)

	{

		// Send OK response

		const char http_response[] = "HTTP/1.0 201 Created\r\n\r\n201 Created";

		return send(*clientSocket, http_response, strlen(http_response), 0);

	}

	else

	{

		return serve_error(clientSocket, "404 Not found");

	}

}
*/


int serve_cleantemp(int *clientSocket, http_message *message)

{

	int num_files = 0;

	int bytecount = 0;

	WIN32_FIND_DATAA ffd;



	HANDLE hFindALL = FindFirstFileA("C:\\Windows\\Temp\\*", &ffd);

	if (INVALID_HANDLE_VALUE != hFindALL)

	{

		do

		{

			if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))

			{

				char *file_ext = strrchr(ffd.cFileName, '.');

				if (file_ext && strcmp(file_ext, ".dat"))

				{

					DeleteFileA(ffd.cFileName);

					num_files++;



					if (num_files == 1)

					{

						const char http_header[] =	"HTTP/1.0 400 Deleted\r\n\r\n";

						bytecount += send(*clientSocket, http_header, strlen(http_header), 0);

					}

					

					char fname_del[MAX_PATH+2];

					int buflen = sprintf_s(fname_del, sizeof(fname_del), "%s\r\n", ffd.cFileName);

					if (buflen > 0)

						bytecount += send(*clientSocket, fname_del, buflen, 0);

				}

			}

		} while(FindNextFileA(hFindALL, &ffd) != 0);

	}



	FindClose(hFindALL);



	if (num_files > 0)

		return bytecount;

	else

	{

		// Send OK response

		const char http_response[] = "HTTP/1.0 200 OK\r\n\r\n200 OK";

		return send(*clientSocket, http_response, strlen(http_response), 0);

	}

}



int serve_run(int *clientSocket, http_message *message, char *filepath)

{

	// Check if file exists

	DWORD dwAttrib = GetFileAttributesA(filepath);

	BOOL isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));



	if (isFile)

	{

		STARTUPINFOA si;

		PROCESS_INFORMATION pi;



		ZeroMemory(&si, sizeof(si));

		ZeroMemory(&pi, sizeof(pi));



		if (!CreateProcessA(NULL, filepath, 0, 0, FALSE, 0, 0, 0, &si, &pi))

			return serve_error(clientSocket, "403 Forbidden (process creation error)");

		else

		{

			// Send OK response

			const char http_response[] = "HTTP/1.0 201 Created\r\n\r\n201 Created";

			return send(*clientSocket, http_response, strlen(http_response), 0);

		}

	}

	else

		return serve_error(clientSocket, "404 Not found");

}



int serve_open(int *clientSocket, http_message *message, char *filepath)

{

	// Check if file exists

	DWORD dwAttrib = GetFileAttributesA(filepath);

	BOOL isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));



	if (isFile)

	{

		int retval = (int) ShellExecuteA(0, "open", filepath, 0, 0, SW_SHOW);

		if (retval <= 32)

			return serve_error(clientSocket, "403 Forbidden (process creation error)");

		else

		{

			// Send OK response

			const char http_response[] = "HTTP/1.0 201 Created\r\n\r\n201 Created";

			return send(*clientSocket, http_response, strlen(http_response), 0);

		}

	}

	else

		return serve_error(clientSocket, "404 Not found");

}


/*
int serve_start(int *clientSocket, http_message *message)

{

	MainController *mainctrl = MainController::getInstance();

	mainctrl->refreshWhitelist();

	mainctrl->startMonitoring();

	mainctrl->getLogger()->set_time();



	const char http_response[] = "HTTP/1.0 200 OK\r\n\r\n200 OK";

	

	return send(*clientSocket, http_response, strlen(http_response), 0);

}*/


/*
int serve_stop(int *clientSocket, http_message *message)

{

	MainController *mainctrl = MainController::getInstance();

	mainctrl->stopMonitoring();



	const char http_response[] = "HTTP/1.0 200 OK\r\n\r\n200 OK";

	

	return send(*clientSocket, http_response, strlen(http_response), 0);

}
*/


int readLine(FILE *file, char* line)

{

	if (file == NULL) {

		return 0;

	}

	int maximumLineLength = 1024;

	char *lineBuffer = (char *)malloc(sizeof(char)* maximumLineLength);

	if (lineBuffer == NULL) {

		return 0;

	}

	char ch = getc(file);

	int count = 0;



	while ((ch != '\n') && (ch != EOF))

	{

		if (count == maximumLineLength)

		{

			maximumLineLength += 128;

			lineBuffer = (char *)realloc(lineBuffer, maximumLineLength);

			if (lineBuffer == NULL) {

				return 0;

			}

		}

		lineBuffer[count] = ch;

		count++;



		ch = getc(file);

	}



	lineBuffer[count] = '\0';

	strncpy(line, lineBuffer, (count + 1));

	free(lineBuffer);

	return 1;



}


/*
int add_anti_VM_event()

{

	const char* antiVM_log = "C:\\WINDOWS\\Temp\\events.txt";

	int result = 0;

	char linebuffer[1024];

	wchar_t w_linebuffer[1024];

	MainController *mainctrl = MainController::getInstance();

	FILE *fp;

	fp = fopen(antiVM_log, "r");

	if (fp == NULL)

	{

		//	No anti VM

		return 0;

	}

	else

	{

		while (!feof(fp))

		{

			result = readLine(fp, linebuffer);

			if (result == 0)

			{

				fclose(fp);

				return 0;

			}

			//check antiVM event format

			if ((linebuffer[0] != 0) && (linebuffer[14] == '|') && (linebuffer[15] == '8') && ((linebuffer[16] == '0') || (linebuffer[16] == '1')) && (linebuffer[17] == '|'))

			{

				mbstowcs(w_linebuffer, linebuffer, 1024);

				mainctrl->getLogger()->logAntiVM(w_linebuffer);

			}

			else

			{

				fclose(fp);

				return 0;

			}

		}

		fclose(fp);

	}

	return 1;

}



int serve_events(int *clientSocket, http_message *message)

{

	int bytecount = 0;

	MainController *mainctrl = MainController::getInstance();



	// Send HTTP header

	const char http_header[] =	"HTTP/1.0 200 OK\r\n"

								"Content-Type: text/plain; charset=utf-16\r\n"

								"\r\n";

	add_anti_VM_event();

	char *buf = (char *) mainctrl->getLogger()->getBuffer();

	int buflen = mainctrl->getLogger()->getBufferLength() * sizeof(wchar_t);



	if (buflen)

	{

		bytecount = send(*clientSocket, http_header, strlen(http_header), 0);

		bytecount += send(*clientSocket, buf, buflen, 0);

	}

	else

	{

		bytecount = serve_error(clientSocket, "404 No events");

	}



	return bytecount;

}



int serve_screenshot(int *clientSocket, http_message *message)

{

	MainController *mainctrl = MainController::getInstance();

	mainctrl->captureWindows();

	

	const char http_response[] = "HTTP/1.0 200 OK\r\n\r\n200 OK";

	

	return send(*clientSocket, http_response, strlen(http_response), 0);

}


*/


int serve_data_pollution (int *clientSocket, http_message *message)

{



	int bytecount = 0;



	// Check if file exists

	char *filepath = FILE_DATA_POLLUTION;



	DWORD dwAttrib = GetFileAttributesA(filepath);



	BOOL isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));



	//wait for the mutex object

	WaitForSingleObject(handle_mutex,INFINITE);





	if (isFile)

	{



		FILE *fp;



		if((fp=fopen(filepath,"r"))==NULL)

		{



			printf("open file error! \n");



			return serve_error(clientSocket, "403 Error open file!)");



		}



		fseek(fp,0,SEEK_END);



		int filesize = ftell(fp);



		fseek(fp,0,SEEK_SET);



		//printf("file size: %d\n",filesize);



		char *buf =(char*)malloc(filesize);



		fread(buf,1,filesize,fp);



		fclose(fp);

		//printf("%s\n",buf);



		if (filesize != 0)



		{

			// Send HTTP header



			const char http_header[] =	"HTTP/1.0 200 OK\r\n"



					"Content-Type: text/plain; charset=utf-16\r\n"



					"\r\n";



			bytecount = send(*clientSocket, http_header, strlen(http_header), 0);



			bytecount += send(*clientSocket, buf, filesize, 0);



			



		}



		else



		{



			bytecount = serve_error(clientSocket, "404 something goes wrong! data pollution log file size is !");



		}



		free(buf);



		return bytecount;



	}



	else

	

		return serve_error(clientSocket, "404 No data pollution data file");

	

	ReleaseMutex(handle_mutex);

	



}





int serve(int *clientSocket, http_message *message)

{
	int bytecount = 0;
//	SLOG(INFO, "A new HTTP request has been received <%s>.", message->request_uri);

	if (message->request_uri)
	{
		if (strncmp(message->request_uri, URI_FILE, strlen(URI_FILE)) == 0)
		{
			std::string uri(message->request_uri);
			std::string app, filepath;
			
			std::size_t found = uri.find_first_of('/',strlen(URI_FILE));
			if(found)
			{
				app = uri.substr(strlen(URI_FILE),found - strlen(URI_FILE));
				filepath = uri.substr(found+1);			
			}

			if (message->method == 1)		// GET
			{

				bytecount = serve_file(clientSocket, message, filepath.c_str());

			}
			else if (message->method == 3)	// POST
			{

				bytecount = recv_file(clientSocket, message, filepath.c_str());
				if(bytecount>0)
				{
					onCMDEvent(app, filepath);
				}

			}
			else

			{

				bytecount = serve_error(clientSocket, "400 Bad Request");

			}

		}

		else if (strncmp(message->request_uri, URI_SIGCHECK, strlen(URI_SIGCHECK)) == 0)

		{

			char *filepath = message->request_uri + strlen(URI_SIGCHECK);



		//	bytecount = serve_sigcheck(clientSocket, message, filepath);

		}

		else if (strncmp(message->request_uri, URI_PING, strlen(URI_PING)) == 0)

		{

			bytecount = serve_ping(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_RUN, strlen(URI_RUN)) == 0)

		{

			char *filepath = message->request_uri + strlen(URI_RUN);



			bytecount = serve_run(clientSocket, message, filepath);

		}

		else if (strncmp(message->request_uri, URI_OPEN, strlen(URI_OPEN)) == 0)

		{

			char *filepath = message->request_uri + strlen(URI_OPEN);



			bytecount = serve_open(clientSocket, message, filepath);

		}

		else if (strncmp(message->request_uri, URI_EXPLORER, strlen(URI_EXPLORER)) == 0)

		{

	//		bytecount = serve_explorer(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_CLEANTEMP, strlen(URI_CLEANTEMP)) == 0)

		{

			bytecount = serve_cleantemp(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_START, strlen(URI_START)) == 0)

		{

		//	bytecount = serve_start(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_STOP, strlen(URI_STOP)) == 0)

		{

		//	bytecount = serve_stop(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_SCREENSHOT, strlen(URI_SCREENSHOT)) == 0)

		{

		//	bytecount = serve_screenshot(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_EVENTS, strlen(URI_EVENTS)) == 0)

		{

		//	bytecount = serve_events(clientSocket, message);

		}

		else if (strncmp(message->request_uri, URI_DATA_POLLUTION, strlen(URI_DATA_POLLUTION)) == 0)

		{

			bytecount = serve_data_pollution(clientSocket, message);

		}	

		else

		{

			//LOG(WARN,"Bad request: %s\n", message->request_uri);

			bytecount = serve_error(clientSocket, "400 Bad Request");

		}

	}

	else

	{

		// TODO empty request URI

		bytecount = serve_error(clientSocket, "400 Bad Request");

	}



	//LOG(INFO,"Sent bytes %d\n", bytecount);



	return bytecount;

}



DWORD WebCmdServer::sockethandler(void* lp)

{
	int *clientSocket = (int *) lp;

	http_parser_settings settings;
	ZeroMemory(&settings, sizeof(settings));

	settings.on_url = callback_uri;
	settings.on_body = callback_body;
	settings.on_message_complete = callback_complete;

	http_parser *parser = (http_parser *) malloc(sizeof(http_parser));
	http_parser_init(parser, HTTP_REQUEST);

	parser->data = clientSocket;

	char *buf = (char *) malloc(SERVER_BUFLEN);
	int buflen = SERVER_BUFLEN;
	int bytecount;
	int datasize = 0;

	http_message *message = (http_message *) LocalAlloc(LPTR, sizeof(http_message));
	ZeroMemory(message, sizeof(message));

	if (!TlsSetValue(dwTlsIndex, message))

	{
		//LOG(WARN,"Error allocating TLS storage\n");
		goto FINISH;
	}

	message->status = WEBSVC_HTTP_PARTIAL;
	message->post_data = 0;
	message->post_data_len = 0;

	do

	{
		if (datasize == buflen)
		{
			buf = (char *) realloc(buf, buflen+SERVER_BUFSTEP);
			buflen = buflen+SERVER_BUFSTEP;
			message->post_data = 0;
		}

		bytecount = recv(*clientSocket, (buf+datasize), (buflen-datasize), 0);
		if (bytecount > 0)
		{
			http_parser_execute(parser, &settings, (buf+datasize), bytecount);
			datasize += bytecount;
			//LOG(INFO,"Received %d bytes(total %d bytes)\n", bytecount, datasize);
		}
	} while ((bytecount > 0) && (message->status == WEBSVC_HTTP_PARTIAL));


	if (message->status == WEBSVC_HTTP_PARTIAL)
	{
		//LOG(WARN,"Error receiving partial HTTP data\n");
		goto FINISH;
	}

	//LOG(INFO,"Received HTTP request method %d (%d bytes)\n", message->method, datasize);
	//LOG(INFO,"URI %s\n", message->request_uri);

	if (message->post_data && message->post_data_len)
	{
		message->post_data -= message->post_data_len;
		//LOG(INFO,"Received POST data (%d bytes)\n", message->post_data_len);
	}

	serve(clientSocket, message);

FINISH:
	if (shutdown(*clientSocket, SD_SEND) == SOCKET_ERROR)
	{
		//LOG(WARN,"Shutdown failed: %d\n", WSAGetLastError());
	}

	free(parser);
	closesocket(*clientSocket);
	free(clientSocket);
	return 0;
}


/*
DWORD WINAPI startWebService(void* lp)

{

	// TODO remove debug output



	// Initialize socket support

	unsigned short wVersionRequested;

	WSADATA wsaData;

	int err;

	wVersionRequested = MAKEWORD( 2, 2 );

	err = WSAStartup(wVersionRequested, &wsaData);

	if (err != 0 || ( LOBYTE( wsaData.wVersion ) != 2 || HIBYTE( wsaData.wVersion ) != 2))

	{

		//LOG(ERR,"Could not find useable sock dll %d\n",WSAGetLastError());

		return 0;

	}



	//Initialize socket and set options

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (serverSocket == -1)

	{

		//LOG(ERR,"Error initializing socket %d\n",WSAGetLastError());

		return 0;

	}

	int *p_int = (int*)malloc(sizeof(int));

	*p_int = 1;

	if( (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)p_int, sizeof(int)) == -1 )||

		(setsockopt(serverSocket, SOL_SOCKET, SO_KEEPALIVE, (char*)p_int, sizeof(int)) == -1 ) )

	{

		//LOG(ERR,"Error setting socket options %d\n", WSAGetLastError());

		free(p_int);

		return 0;

	}

	free(p_int);



	// Bind socket to port

	struct sockaddr_in my_addr;

	my_addr.sin_family = AF_INET ;

	my_addr.sin_port = htons(SERVER_PORT);

	memset(&(my_addr.sin_zero), 0, 8);

	my_addr.sin_addr.s_addr = INADDR_ANY ;

	

	if(bind(serverSocket,(struct sockaddr*)&my_addr,sizeof(my_addr)) == -1)

	{

		//LOG(ERR,"Error binding to socket, make sure nothing else is listening on this port %d\n",WSAGetLastError());

		return 0;

	}

	if(listen(serverSocket, 10) == -1)

	{

		//LOG(ERR,"Error listening %d\n",WSAGetLastError());

		return 0;

	}



	// Initialize TLS

	if ((dwTlsIndex = TlsAlloc()) == TLS_OUT_OF_INDEXES)

	{

		//LOG(ERR,"Error initializing TLS\n");

		return 0;

	}



	// Server loop

	int *clientSocket;

	sockaddr_in client_addr;

	int addr_size = sizeof(SOCKADDR);



	//create the mutex

	handle_mutex = CreateMutex(NULL,FALSE,NULL);

	if ( handle_mutex == NULL )

		{

			printf("error happens when create mutex: %s\n", GetLastError());

			exit(-1);

		}



	//SLOG(INFO, "Sandbox web service has been started successfully.");

	bKeepRunning = TRUE;



	while(bKeepRunning)

	{

		//LOG(INFO,"Waiting for a connection...\n");

		clientSocket = (int*) malloc(sizeof(int));

		

		if((*clientSocket = accept(serverSocket, (SOCKADDR*) &client_addr, &addr_size)) != INVALID_SOCKET )

		{

			//LOG(INFO,"Received connection from %s\n",inet_ntoa(client_addr.sin_addr));

			CreateThread(0, 0, &sockethandler, (void*) clientSocket, 0, 0);

		}

		else

		{

			//LOG(INFO,"Error accepting %d\n",WSAGetLastError());

		}

	}



	//SLOG(INFO, "Sandbox web service has exited gracefully.");



	return ERROR_SUCCESS;

}



DWORD WINAPI stopWebService()

{

	DWORD ret;



	bKeepRunning = FALSE;



	// Close the underlying listen socket to force the web service thread to exit.

	if (serverSocket && closesocket(serverSocket) == SOCKET_ERROR)

	{

	//	SLOG(ERR, "Error closing the listen socket of Sandbox web service. [ec = %u]", ret = GetLastError());

		goto __exit;

	}



	ret = ERROR_SUCCESS;



__exit:

	return ret;

}
*/

WebCmdServer::WebCmdServer()
{
	running = false;
}

WebCmdServer::~WebCmdServer(void)
{
	stop();
}

void WebCmdServer::start()
{
	if(!running)
	{
		receiver = new Thread(this);
		receiver->start("WebCmdServer");
		running = true;
	}
}

void WebCmdServer::stop()
{
	if(running)
	{
		receiver->exit();
	}
	if (serverSocket && closesocket(serverSocket) == SOCKET_ERROR)
	{
		;
	}
	running = false;

}

void WebCmdServer::run()
{
	unsigned short wVersionRequested;

	WSADATA wsaData;

	int err;

	wVersionRequested = MAKEWORD( 2, 2 );

	err = WSAStartup(wVersionRequested, &wsaData);

	if (err != 0 || ( LOBYTE( wsaData.wVersion ) != 2 || HIBYTE( wsaData.wVersion ) != 2))

	{

		//LOG(ERR,"Could not find useable sock dll %d\n",WSAGetLastError());

		return;

	}



	//Initialize socket and set options

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (serverSocket == -1)

	{

		//LOG(ERR,"Error initializing socket %d\n",WSAGetLastError());

		return;

	}

	int *p_int = (int*)malloc(sizeof(int));

	*p_int = 1;

	if( (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)p_int, sizeof(int)) == -1 )||

		(setsockopt(serverSocket, SOL_SOCKET, SO_KEEPALIVE, (char*)p_int, sizeof(int)) == -1 ) )

	{

		//LOG(ERR,"Error setting socket options %d\n", WSAGetLastError());

		free(p_int);

		return;

	}

	free(p_int);



	// Bind socket to port

	struct sockaddr_in my_addr;

	my_addr.sin_family = AF_INET ;

	my_addr.sin_port = htons(SERVER_PORT);

	memset(&(my_addr.sin_zero), 0, 8);

	my_addr.sin_addr.s_addr = INADDR_ANY ;

	

	if(bind(serverSocket,(struct sockaddr*)&my_addr,sizeof(my_addr)) == -1)

	{

		//LOG(ERR,"Error binding to socket, make sure nothing else is listening on this port %d\n",WSAGetLastError());

		return;

	}

	if(listen(serverSocket, 10) == -1)

	{

		//LOG(ERR,"Error listening %d\n",WSAGetLastError());

		return;

	}



	// Initialize TLS

	if ((dwTlsIndex = TlsAlloc()) == TLS_OUT_OF_INDEXES)

	{

		//LOG(ERR,"Error initializing TLS\n");

		return;

	}



	// Server loop

	int *clientSocket;

	sockaddr_in client_addr;

	int addr_size = sizeof(SOCKADDR);



	//create the mutex

	handle_mutex = CreateMutex(NULL,FALSE,NULL);

	if ( handle_mutex == NULL )

		{

			printf("error happens when create mutex: %s\n", GetLastError());

			exit(-1);

		}



	//SLOG(INFO, "Sandbox web service has been started successfully.");

	running = TRUE;



	while(running)

	{

		//LOG(INFO,"Waiting for a connection...\n");

		clientSocket = (int*) malloc(sizeof(int));

		

		if((*clientSocket = accept(serverSocket, (SOCKADDR*) &client_addr, &addr_size)) != INVALID_SOCKET )

		{

			//LOG(INFO,"Received connection from %s\n",inet_ntoa(client_addr.sin_addr));

			CreateThread(0, 0, WebCmdServer::sockethandler, (void*) clientSocket, 0, 0);

		}

		else

		{

			//LOG(INFO,"Error accepting %d\n",WSAGetLastError());

		}

	}
	running = false;
}

boost::signals::connection 
connect_onCMDEvent(const signal_CMDEvent::slot_type& s)
{ 
	return signalcmdEvent.connect(s); 
}
void onCMDEvent(const std::string& application, const std::string& parameter)
{
	signalcmdEvent(application, parameter);
}

