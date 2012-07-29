/*
FastCraft - Minecraft SMP Server
Copyright (C) 2011 - 2012 Michael Albrecht aka Sprenger120

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "NetworkIn.h"
#include "FCRuntimeException.h"
#include <Poco/Net/NetException.h>
#include <Poco/ByteOrder.h>
#include "MinecraftServer.h"


NetworkIn::NetworkIn(StreamSocket& r,MinecraftServer* pMCServer) :
_rSocket(r)
{
	_pMCServer = pMCServer;
}

char NetworkIn::readByte() {
	try {
		read(1,_sReadBuffer);
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}

	return _sReadBuffer[0];
}

bool NetworkIn::readBool() {
	char iByte;

	try {
		iByte = readByte();
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}


	if (iByte == 0) {
		return false;
	}else{
		return true;
	}
}

short NetworkIn::readShort() {
	try {
		read(2,_sReadBuffer);
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}

	return (short(_sReadBuffer[0])<<8 & 0xFF00) | 
			(short(_sReadBuffer[1]) & 0x00FF);
}

int NetworkIn::readInt() {
	try {
		read(4,_sReadBuffer);
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}

	return (int(_sReadBuffer[0])<<24 & 0xFF000000 )| 
		(int(_sReadBuffer[1])<<16 & 0x00FF0000 )|
		(int(_sReadBuffer[2])<<8 & 0x0000FF00 )|
		(int(_sReadBuffer[3]) & 0x000000FF );
}


long long NetworkIn::readInt64() {
	try {
		read(8,_sReadBuffer);
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}


	return (
		    ((((long long)_sReadBuffer[0])<<56) & 0xFF00000000000000) |
            ((((long long)_sReadBuffer[1])<<48) & 0xFF000000000000) |
			((((long long)_sReadBuffer[2])<<40) & 0xFF0000000000) |
			((((long long)_sReadBuffer[3])<<32) & 0xFF00000000) |
			((((long long)_sReadBuffer[4])<<24) & 0xFF000000) |
			((((long long)_sReadBuffer[5])<<16) & 0xFF0000) |
			((((long long)_sReadBuffer[6])<<8)  & 0xFF00) |
			(((long long)_sReadBuffer[7])       & 0xFF)
		  );
}

float NetworkIn::readFloat() {
	try {
		_ItF.i = readInt();
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}

	return _ItF.d;
}


double NetworkIn::readDouble() {
	try {
		_ItD.i = readInt64();
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}
	return _ItD.d;
}

string NetworkIn::readString() {
	string sOutput("");
	short iDataLenght;

	try {
		iDataLenght = readShort();
		if (iDataLenght < 0) {throw FCRuntimeException("Illegal length field");}

		for(short x = 0;x<=iDataLenght-1;x++) {
			readByte();
			sOutput.append(1,readByte());
		}
	} catch(FCRuntimeException& ex) {
		ex.rethrow();
	}

	return sOutput;
}

std::pair<char*,short> NetworkIn::readByteArray() {
	short iLen = readShort();
	if (iLen <= 0) {throw FCRuntimeException("Illegal length field");}

	char* pStr = new char[iLen];
	read(iLen,pStr);

	return std::make_pair(pStr,iLen);
}


void NetworkIn::read(int iLenght,char* pBuffer) {
	int iReadedLenght = 0;
	int iUnderflowCount=0;
	bool fUnderflow = false;

	while ( iReadedLenght < iLenght) {
		try {
			switch(fUnderflow) {
			case false:
				iReadedLenght = _rSocket.receiveBytes(pBuffer,iLenght);
				break;
			case true:
				iReadedLenght += _rSocket.receiveBytes(&pBuffer[iReadedLenght], iLenght - iReadedLenght);
				break;
			}
		}catch(Poco::Net::ConnectionAbortedException) {
			throw FCRuntimeException("Connection aborted",false);
		}catch(Poco::Net::InvalidSocketException) {
			throw FCRuntimeException("Invalid socket",false);
		}catch(Poco::TimeoutException) {
			throw FCRuntimeException("Timeout",false);
		}catch(Poco::Net::ConnectionResetException) {
			throw FCRuntimeException("Connection reset",false);
		}catch(Poco::IOException) {
			throw FCRuntimeException("I/O Error",false);
		}
		if (iReadedLenght != iLenght) {
			iUnderflowCount++;
			if (iUnderflowCount > 10) {
				throw FCRuntimeException("Timeout",false);
			}
			fUnderflow = true;
		}
	}
	_pMCServer->_iReadTraffic += iLenght;
}