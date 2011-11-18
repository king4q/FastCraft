/*
FastCraft - Minecraft SMP Server
Copyright (C) 2011  Michael Albrecht aka Sprenger120

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef _FASTCRAFTHEADER_NETWORKIO
#define _FASTCRAFTHEADER_NETWORKIO
#include <Poco/Net/StreamSocket.h>
#include <string>

using Poco::Net::StreamSocket;
using std::string;

//This class sets the streamsocket automaticly to blocking
class NetworkIO {
private:
	StreamSocket _Connection;
	string _sBuffer;
	char _charBuffer[1024];

	bool _fConnected;
	const int _iTimeout;

	static int _iReadTraffic;
	static int _iWriteTraffic;
public:
	NetworkIO(); //Init NetworkIO without connection
	NetworkIO(StreamSocket&); //Init NetworkIO with connection
	~NetworkIO();

	//Write part
	void addByte(char);
	void addBool(bool);
	void addShort(short);
	void addInt(int);
	void addInt64(long long);
	void addFloat(float);
	void addDouble(double);
	void addString(string);

	//Read part
	char readByte(); 
	bool readBool();
	short readShort();
	int readInt();
	long long readInt64();
	float readFloat();
	double readDouble();
	string readString();

	void Flush();

	//Connection adding / closing
	bool isConnected();
	void closeConnection();
	void newConnection(StreamSocket&);

	//Traffic
	static int getReadTraffic();
	static int getWriteTraffic();
	static int getIOTraffic();
private:
	bool exceptionSaveReading(int); //Reads data from tcp stream, checks all thrown exceptions, increase readtraffic
}; 
#endif