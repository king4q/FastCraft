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

#include "PlayerThread.h"
#include "EntityID.h"
#include "SettingsHandler.h"
#include "ServerTime.h"
#include "PlayerPool.h"
#include "Constants.h"
#include "World.h"
#include "ItemInfoStorage.h"
#include "PlayerPoolEvent.h"
#include "MathHelper.h"
#include <sstream>
#include <istream>
#include <Poco/Timespan.h>
#include <Poco/Net/NetException.h>
#include <Poco/Stopwatch.h>
#include <math.h>
#include "Structs.h"
#include "ChunkMath.h"

using Poco::Thread;
using std::cout;
using std::endl;
using std::dec;

PlayerThread::PlayerThread(PlayerPool* pPoolMaster,
	World& rWorld,
	PackingThread& rPackingThread
	) : 
_sName(""),
	_sIP(""),
	_sLeaveMessage(""),
	_Connection(),
	_sTemp(""),
	_sConnectionHash(""),
	_rWorld(rWorld),

	_NetworkOutRoot(),
	_NetworkInRoot(_Connection),
	_NetworkWriter(_NetworkOutRoot.getLowQueue(),_NetworkOutRoot.getHighQueue(),_Connection,this),

	_Web_Session("session.minecraft.net"),
	_Web_Response(),
	_Flags(),
	_Rand(),
	_ChunkProvider(rWorld,_NetworkOutRoot,rPackingThread,this),
	_threadNetworkWriter("NetworkWriter"),
	_Inventory(_NetworkOutRoot,_NetworkInRoot),
	_vSpawnedEntities(0)
{
	_Coordinates.OnGround = false;
	_Coordinates.Pitch = 0.0F;
	_Coordinates.Stance = 0.0;
	_Coordinates.X = 0.0;
	_Coordinates.Y = 0.0;
	_Coordinates.Yaw = 0.0F;
	_Coordinates.Z = 0.0;

	_TimeJobs.LastTimeSend = 0L;
	_TimeJobs.LastKeepAliveSend = 0L;
	_TimeJobs.LastHandleMovement = 0L;
	_TimeJobs.LastMovementSend = 0L;
	_TimeJobs.LastSpeedCalculation=0L;
	_TimeJobs.LastPositionCheck=0L;
	_dRunnedMeters=0.0;

	_iEntityID=0;
	_Spawned_PlayerInfoList = 0;
	_pPoolMaster = pPoolMaster;

	_fSpawned = false;
	_fAssigned = false;
	_iThreadTicks = 0;

	//Start NetworkWriter Thread
	_threadNetworkWriter.start(_NetworkWriter);
}

int PlayerThread::_PlayerCount = 0;

PlayerThread::~PlayerThread() {
	_NetworkWriter.clearQueues();
}


bool PlayerThread::isAssigned() {
	return _fAssigned;
}

string PlayerThread::getUsername() {
	return _sName;
}

string PlayerThread::getIP() {
	return _sIP;
}

EntityCoordinates PlayerThread::getCoordinates() {
	return _Coordinates;
}

int PlayerThread::getConnectedPlayers() {
	return _PlayerCount;
}

void PlayerThread::run() {
	unsigned char iPacket;
	Poco::Stopwatch Timer;

	Timer.start();
	while (1) {
		if (!isAssigned()) {
			while(!isAssigned()) {
				Thread::sleep(50);
			}
			Timer.reset();
		} 


		try {
			//Thread Ticks
			Timer.stop();
			_iThreadTicks += Timer.elapsed() / 1000;
			Timer.reset();
			Timer.start();


			if (!_fSpawned && _iThreadTicks >= FC_MAXLOGINTIME) {
				Disconnect("Login timed out");
				continue;
			}

			Interval_KeepAlive(); 
			Interval_Time();
			Interval_HandleMovement();
			Interval_Movement();
			Interval_CalculateSpeed();
			Interval_CheckPosition();

			iPacket = _NetworkInRoot.readByte();

			//cout<<"Package recovered:"<<std::hex<<int(iPacket)<<"\n";
			switch (iPacket) {
			case 0x0:
				Packet0_KeepAlive();
				break;
			case 0x1:
				if (isSpawned()) {
					Disconnect("Login already done!");
					continue;
				}
				Packet1_Login();
				break;
			case 0x2:
				if (isSpawned()) {
					Disconnect("Login already done!");
					continue;
				}
				Packet2_Handshake();
				break;
			case 0x3:
				Packet3_Chat();
				break;
			case 0xa: 
				Packet10_Player();
				break;
			case 0xb:
				Packet11_Position();
				break;
			case 0xc: 
				Packet12_Look();
				break;
			case 0xd: 
				Packet13_PosAndLook();
				break;
			case 0xf: 
				Packet15_PlayerBlockPlacement();
				break;
			case 0x10: 
				Packet16_HoldingChange();
				break;
			case 0x12: 
				Packet18_Animation();
				break;
			case 0x13: 
				Packet19_EntityAction();
				break;
			case 0x65: 
				Packet101_CloseWindow();
				break;
			case 0x66:
				Packet102_WindowClick();
				break;
			case 0xFE: //Server List Ping
				Packet254_ServerListPing();
				break;
			case 0xFF: //Disconnect
				Packet255_Disconnect();
				break;
			default: 
				Disconnect("Unknown packet!");
				cout<<"Unknown packet received! 0x"<<std::hex<<int(iPacket)<<endl;
				break;
			}
		} catch (Poco::RuntimeException) {
			Disconnect(FC_LEAVE_OTHER);
		}

	}
}

void PlayerThread::Disconnect(string sReason) {
	if(isSpawned()) {
		_fSpawned=false;
		_NetworkWriter.clearQueues();

		stringstream strStrm;
		strStrm<<_sName<<" was kicked for: "<<sReason; 
		cout<<strStrm.str()<<"\n"; //Writer kick message to chat

		NetworkOut Out = _NetworkOutRoot.New();
		Out.addByte(0xFF);
		Out.addString(sReason);
		Out.Finalize(FC_QUEUE_HIGH);

		ProcessQueue();
		Disconnect(FC_LEAVE_KICK);

		PlayerPoolEvent Event(_Coordinates,"�6" + strStrm.str(),this);
		_pPoolMaster->Event(Event); //Write to players chat
	}else{
		NetworkOut Out = _NetworkOutRoot.New();
		Out.addByte(0xFF);
		Out.addString(sReason);
		Out.Finalize(FC_QUEUE_HIGH);
		ProcessQueue();
		Disconnect(FC_LEAVE_QUIT);
	}
}

void PlayerThread::Disconnect(char iLeaveMode) {
	if (!_fAssigned) { return; }

	if (isSpawned() || iLeaveMode==FC_LEAVE_KICK) {
		_ChunkProvider.HandleDisconnect();
		_Inventory.HandleDisconnect();
		_PlayerCount--;


		PlayerPoolEvent Event(false,_sName,this);
		switch (iLeaveMode) {
		case FC_LEAVE_KICK:
			_pPoolMaster->Event(Event);
			break;
		case FC_LEAVE_QUIT:		
			cout<<_sName<<" quit ("<<_sLeaveMessage<<")"<<"\n";
			_pPoolMaster->Event(Event);
			break;
		case FC_LEAVE_OTHER:
			cout<<_sName<<" quit (unknown reason)"<<"\n";
			_pPoolMaster->Event(Event);
			break;
		default:
			cout<<"***INTERNAL SERVER WARNING: Unknown disconnect mode"<<"\n";
		}
	}

	_Flags.clear();

	_fSpawned = false;
	_fAssigned = false;
	_sName.clear();
	_sIP.clear();
	if (iLeaveMode!=FC_LEAVE_KICK) {_NetworkWriter.clearQueues();}

	_TimeJobs.LastKeepAliveSend = 0L;
	_TimeJobs.LastTimeSend = 0L;
	_TimeJobs.LastHandleMovement = 0L;
	_TimeJobs.LastMovementSend = 0L;
	_TimeJobs.LastSpeedCalculation = 0L;
	_TimeJobs.LastPositionCheck=0L;
	_dRunnedMeters=0;
	_Spawned_PlayerInfoList=0;

	_Connection.close();
	_vSpawnedEntities.clear();
}


void PlayerThread::Connect(Poco::Net::StreamSocket& Sock) {
	if (_fAssigned) {
		cout<<"***INTERNAL SERVER WARNING: PlayerPool tryed to assign an already assigned player thread"<<"\n";
		Disconnect(FC_LEAVE_OTHER);
	}
	_fAssigned=true;
	_Connection = Sock; 
	_Connection.setLinger(true,5);
	_Connection.setNoDelay(false);
	_Connection.setBlocking(true);

	_Inventory.clear();
	_sIP.assign(_Connection.peerAddress().toString());
	_iThreadTicks = 0;
}

string PlayerThread::generateConnectionHash() {
	std::stringstream StringStream;

	StringStream<<std::hex<<_Rand.next();
	if(StringStream.str().length() % 2) {
		StringStream<<"0";
	}
	StringStream<<std::hex<<_Rand.next();
	_sConnectionHash.assign(StringStream.str());
	return _sConnectionHash;
}

void PlayerThread::Interval_Time() {
	if (!isSpawned()) {return;}
	if (_TimeJobs.LastTimeSend + FC_INTERVAL_TIMESEND <= getTicks()) {
		_TimeJobs.LastTimeSend = getTicks();
		sendTime();
	}
}

void PlayerThread::Interval_HandleMovement() {
	if (!isSpawned()) {return;}
	if (_TimeJobs.LastHandleMovement + FC_INTERVAL_HANDLEMOVEMENT <= getTicks()) {
		_TimeJobs.LastHandleMovement = getTicks();
		_ChunkProvider.HandleMovement(_Coordinates);

		/* - Looks like a cristmas tree, uncomment to have a look at it
		_highNetwork.Lock();
		for (int z=0;z<=3;z++) {
		for (int x=0;x<=3;x++) {
		for (char y=33;y<=50;y++) {

		_highNetwork.addByte(0x3D);

		_highNetwork.addInt(2000);
		_highNetwork.addInt(x);
		_highNetwork.addByte(y);
		_highNetwork.addInt(z);

		_highNetwork.addInt(0);

		_highNetwork.Flush();
		}


		}

		}
		_highNetwork.UnLock();
		*/
	}
}

void PlayerThread::sendTime() {
	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x4);
	Out.addInt64(ServerTime::getTime());
	Out.Finalize(FC_QUEUE_HIGH);
}

long long PlayerThread::getTicks() {
	return _iThreadTicks;
}


void PlayerThread::sendClientPosition() {
	NetworkOut Out = _NetworkOutRoot.New();

	Out.addByte(0x0D);
	Out.addDouble(_Coordinates.X);

	switch(_Flags.isCrouched()) {//FastCraft will never modify Stance so we have to calculate it
	case true:
		_Coordinates.Stance = _Coordinates.Y + 1.5; 
		break;
	case false:
		_Coordinates.Stance = _Coordinates.Y + 1.6;
		break;
	}
	Out.addDouble(_Coordinates.Stance);
	Out.addDouble(_Coordinates.Y);
	Out.addDouble(_Coordinates.Z);
	Out.addFloat(_Coordinates.Yaw);
	Out.addFloat(_Coordinates.Pitch);
	Out.addBool(_Coordinates.OnGround);

	Out.Finalize(FC_QUEUE_HIGH);
}

void PlayerThread::Interval_KeepAlive() {
	if (!isSpawned()){return;}
	if (_TimeJobs.LastKeepAliveSend + FC_INTERVAL_KEEPACTIVE <= getTicks()) { //Send new keep alive
		_TimeJobs.LastKeepAliveSend = getTicks();

		NetworkOut Out = _NetworkOutRoot.New();
		Out.addByte(0x0);
		Out.addInt(_Rand.next());
		Out.Finalize(FC_QUEUE_HIGH);
	}
}

void PlayerThread::UpdateHealth(short iHealth,short iFood,float nSaturation) {
	//Send Health
	_iHealth = fixRange<short>(iHealth,0,20);
	_iFood = fixRange<short>(iFood,0,20);
	_nSaturation = fixRange<float>(iFood,0.0F,5.0F);

	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x8);
	Out.addShort(_iHealth);
	Out.addShort(_iFood);
	Out.addFloat(_nSaturation);
	Out.Finalize(FC_QUEUE_HIGH);
}



template <class T> T PlayerThread::fixRange(T val,T min,T max) {
	if (val > max) {
		return max;
	}

	if (val < min) {
		return min;
	}

	return val;
}


bool PlayerThread::isSpawned() {
	return _fSpawned;
}


void PlayerThread::insertChat(string rString) {
	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x3);
	Out.addString(rString);
	Out.Finalize(FC_QUEUE_HIGH);
}


void PlayerThread::ProcessQueue() {			
	//All elements from high queue
	while (!_NetworkOutRoot.getHighQueue().empty()) {
		string & rJob = _NetworkOutRoot.getHighQueue().front();

		try {
			_Connection.sendBytes(rJob.c_str(),rJob.length()); //Send
		}catch(Poco::Net::ConnectionAbortedException) {
			Disconnect(FC_LEAVE_OTHER);
			return;
		}catch(Poco::Net::InvalidSocketException) {
			Disconnect(FC_LEAVE_OTHER);
			return;
		}catch(Poco::TimeoutException) {
			Disconnect(FC_LEAVE_OTHER);
			return;
		}catch(Poco::Net::ConnectionResetException) {
			Disconnect(FC_LEAVE_OTHER);
			return;
		}catch(Poco::IOException) {
			Disconnect(FC_LEAVE_OTHER);
			return;
		}


		_NetworkOutRoot.getHighQueue().pop();
	}
}

void PlayerThread::Packet0_KeepAlive() {
	_NetworkInRoot.readInt(); //Get id
}

void PlayerThread::Packet1_Login() {
	int iProtocolVersion = 0;

	try {
		//Check minecraft version
		iProtocolVersion = _NetworkInRoot.readInt(); //Protocol Version
		

		if (iProtocolVersion > SettingsHandler::getSupportedProtocolVersion()) {
			Disconnect("Outdated server! Needed Version: " + SettingsHandler::getSupportedMCVersion());
			return;
		}

		if (iProtocolVersion <  SettingsHandler::getSupportedProtocolVersion()) {
			Disconnect("Outdated client! Needed Version: " + SettingsHandler::getSupportedMCVersion());
			return;
		}

		_NetworkInRoot.readString(); //Username (already known)	
		_NetworkInRoot.readInt64();
		_NetworkInRoot.readString();
		_NetworkInRoot.readInt();
		_NetworkInRoot.readInt(); //This are 4 unused byte fields

		//Check premium 
		if (SettingsHandler::isOnlineModeActivated()) {
			string sPath("/game/checkserver.jsp?user=");
			sPath.append(_sName);
			sPath.append("&serverId=");
			sPath.append(_sConnectionHash);

			Poco::Net::HTTPRequest Request ( 
				Poco::Net::HTTPRequest::HTTP_GET, 
				sPath,
				Poco::Net::HTTPMessage::HTTP_1_1);

			_Web_Session.sendRequest(Request);
			std::istream &is = _Web_Session.receiveResponse(_Web_Response);
			string sErg;
			is>>sErg;

			if (sErg.compare("YES") != 0) {
				Disconnect("Failed to verify username!");
				return;
			}
		}

		//Check if there is a player with same name
		PlayerThread* pPlayer = _pPoolMaster->getPlayerByName(_sName,this);
		if (pPlayer != NULL) {
			pPlayer->Disconnect("Logged in from another location.");
		}


		//YES DUDE, you got it !
		_PlayerCount++; //There is an new spawned player
		_iEntityID = EntityID::New(); //Fetch a new ID
		_ChunkProvider.HandleNewPlayer();

		//Set start coordinates
		_Coordinates.X = 0.0;
		_Coordinates.Y = 0.0;
		_Coordinates.Z = 0.0;
		_Coordinates.Stance = 0.0;
		_Coordinates.OnGround = false;
		_Coordinates.Pitch = 0.0F;
		_Coordinates.Yaw = 0.0F;
		_lastCoordinates = _Coordinates;
		CheckPosition(false);
		
		/*
		* Response
		*/
		//Login response
		NetworkOut Out = _NetworkOutRoot.New();

		Out.addByte(0x1);
		Out.addInt(_iEntityID);
		Out.addString("");
		Out.addInt64(SettingsHandler::getMapSeed());
		Out.addString("SUPERFLAT");
		Out.addInt(SettingsHandler::getServerMode());
		Out.addByte(0);
		Out.addByte(SettingsHandler::getDifficulty());
		Out.addByte(SettingsHandler::getWorldHeight());
		Out.addByte((unsigned char)SettingsHandler::getPlayerSlotCount());

		Out.Finalize(FC_QUEUE_HIGH);

		//compass
		Out.addByte(0x6);
		Out.addInt(0); //X
		Out.addInt(0); // Y
		Out.addInt(0); // Z 
		Out.Finalize(FC_QUEUE_HIGH);

		//Time
		sendTime();

		_ChunkProvider.HandleMovement(_Coordinates); //Pre Chunks

		//Health
		UpdateHealth(20,20,5.0F); //Health

		//Inventory
		ItemSlot Item1(1,64);
		ItemSlot Item2(5,64);
		ItemSlot Item3(55,64);
		ItemSlot Item4(360,64);

		_Inventory.setSlot(38,Item1);
		_Inventory.setSlot(37,Item2);
		_Inventory.setSlot(36,Item3);
		_Inventory.setSlot(39,Item4);
		_Inventory.setSlot(40,Item2);
		_Inventory.setSlot(41,Item3);
		_Inventory.synchronizeInventory();

		sendClientPosition();

		insertChat("�dWelcome to FastCraft 0.0.2 Alpha server.");
		ProcessQueue(); //Send login packages

		PlayerPoolEvent Event(true,_sName,this);//Push PlayerPool Join event
		_pPoolMaster->Event(Event);
		_fSpawned = true;	


		//Spawn own name to playerinfo
		PlayerInfoList(true,_sName);

		//Spawn other player
		vector<string> vNames;
		vNames = _pPoolMaster->ListPlayers(59);

		if (vNames.size() > 0) {
			for ( int x = 0;x<= vNames.size()-1;x++) {
				if(vNames[x].compare(_sName)==0) {continue;}//No double spawning of own name
				PlayerInfoList(true,vNames[x]);
			}
		}
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
	cout<<_sName<<" joined ("<<_sIP<<") EID:"<<_iEntityID<<endl;  //Console log
}

void PlayerThread::Packet2_Handshake() {
	try {
		_sName = _NetworkInRoot.readString();
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}

	if (_sName.length() > 16) {
		Disconnect("Username too long");
		return;
	}

	//Send response (Connection Hash)
	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x02);

	if (SettingsHandler::isOnlineModeActivated()) {
		Out.addString(generateConnectionHash());
	}else{
		Out.addString("-");
	}
	Out.Finalize(FC_QUEUE_HIGH);

	ProcessQueue();
}

void PlayerThread::Packet3_Chat() {
	string Message("");

	try {
		Message = _NetworkInRoot.readString();
		if (Message.length() > 100) {
			Disconnect("Received string too long");
			return;
		}
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}

	_sTemp.assign("<");
	_sTemp.append(_sName);
	_sTemp.append("> ");
	_sTemp.append(Message); 

	pushChatEvent(_sTemp);
}

void PlayerThread::Packet10_Player() {
	if (isSpawned()) {
		_ChunkProvider.NextChunk();
	}
	try {
		_Coordinates.OnGround = _NetworkInRoot.readBool();
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet11_Position() {
	EntityCoordinates TmpCoord;
	if (isSpawned()) {
		_ChunkProvider.NextChunk();
	}
	try {
		//Read coordinates in a temporary variable
		TmpCoord.X = _NetworkInRoot.readDouble();
		TmpCoord.Y = _NetworkInRoot.readDouble();
		TmpCoord.Stance = _NetworkInRoot.readDouble();
		TmpCoord.Z = _NetworkInRoot.readDouble();
		TmpCoord.OnGround = _NetworkInRoot.readBool();

		//if X and Z ==  8.5000000000000000 , there is crap in the tcp buffer -> ignore it
		if (TmpCoord.X == 8.5000000000000000 && TmpCoord.Z == 8.5000000000000000) { 
			return;
		}else{
			_dRunnedMeters += MathHelper::distance2D(_Coordinates,TmpCoord);
			_Coordinates.X = TmpCoord.X;
			_Coordinates.Y = TmpCoord.Y;
			_Coordinates.Stance = TmpCoord.Stance;
			_Coordinates.Z = TmpCoord.Z;
			_Coordinates.OnGround = TmpCoord.OnGround;
		}
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet12_Look() {
	if (isSpawned()) {
		_ChunkProvider.NextChunk();
	}
	try {
		_Coordinates.Yaw = _NetworkInRoot.readFloat();
		_Coordinates.Pitch = _NetworkInRoot.readFloat();
		_Coordinates.OnGround = _NetworkInRoot.readBool();
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet13_PosAndLook() {
	EntityCoordinates TmpCoord;

	if (isSpawned()) {
		_ChunkProvider.NextChunk();
	}

	//Read coordinates in a temporary variable
	try {
		TmpCoord.X = _NetworkInRoot.readDouble();
		TmpCoord.Y = _NetworkInRoot.readDouble();
		TmpCoord.Stance = _NetworkInRoot.readDouble();
		TmpCoord.Z = _NetworkInRoot.readDouble();
		TmpCoord.Yaw = _NetworkInRoot.readFloat();
		TmpCoord.Pitch = _NetworkInRoot.readFloat();
		TmpCoord.OnGround = _NetworkInRoot.readBool();

		//if X and Z ==  8.5000000000000000 , there is crap in the tcp buffer -> ignore it
		if (TmpCoord.X == 8.5000000000000000 && TmpCoord.Z == 8.5000000000000000) { 
			return;
		}else{
			_dRunnedMeters += MathHelper::distance2D(_Coordinates,TmpCoord);
			_Coordinates = TmpCoord;
		}
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet254_ServerListPing() {
	_sTemp.clear();
	_sTemp.assign(SettingsHandler::getServerDescription()); //Server Description
	_sTemp.append("�");
	Poco::NumberFormatter::append(_sTemp,_PlayerCount); //Player count
	_sTemp.append("�");
	Poco::NumberFormatter::append(_sTemp,SettingsHandler::getPlayerSlotCount()); //player slots

	Disconnect(_sTemp);
}

void PlayerThread::Packet255_Disconnect() {
	try {
		_sLeaveMessage = _NetworkInRoot.readString();
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
	Disconnect(FC_LEAVE_QUIT);
}

void PlayerThread::pushChatEvent(string& rString) {
	PlayerPoolEvent Event(_Coordinates,rString,this);
	_pPoolMaster->Event(Event);
}

void PlayerThread::PlayerInfoList(bool fSpawn,string Name) {
	if (fSpawn) {
		if (_Spawned_PlayerInfoList == 60) {return;} //Workaround for a Minecraft render bug
		_Spawned_PlayerInfoList++;
	}else{
		_Spawned_PlayerInfoList--;
	}

	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0xc9);
	Out.addString(Name);
	Out.addBool(fSpawn);
	Out.addShort(10);
	Out.Finalize(FC_QUEUE_HIGH);
}

void PlayerThread::Packet16_HoldingChange() {
	short iSlot;
	try{
		iSlot = _NetworkInRoot.readShort();
		if (iSlot < 0 || iSlot > 8) {
			Disconnect("Illegal holding slotID");
			return;
		}
		_Inventory.HandleSelectionChange(iSlot);

		PlayerPoolEvent Event(0,_Inventory.getSlot(36+iSlot),this);
		_pPoolMaster->Event(Event);
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet101_CloseWindow() {
	try {	
		char iWinID = _NetworkInRoot.readByte();
		if (iWinID == 0) {
			_Inventory.HandleWindowClose(_pPoolMaster);
		}
	} catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet102_WindowClick() {
	ItemSlot aHeld[5];

	aHeld[0] = _Inventory.getSlot(36 + _Inventory.getSlotSelection());
	aHeld[1] = _Inventory.getSlot(8);
	aHeld[2] = _Inventory.getSlot(7);
	aHeld[3] = _Inventory.getSlot(6);
	aHeld[4] = _Inventory.getSlot(5);

	_Inventory.HandleWindowClick(this);	
	//_Inventory.synchronizeInventory();


	if (aHeld[0].getItemID() != _Inventory.getSlot(36 + _Inventory.getSlotSelection()).getItemID()) { //Check if held item were changed
		PlayerPoolEvent Event(0,_Inventory.getSlot(36 + _Inventory.getSlotSelection()),this);
		_pPoolMaster->Event(Event);
	}

	int s=8;
	for (int x=1;x<=4;x++) {
		if (aHeld[x].getItemID() != _Inventory.getSlot(s).getItemID()) {//Check if boots were changed
			PlayerPoolEvent Event(x,_Inventory.getSlot(s),this);
			_pPoolMaster->Event(Event);
		}
		s--;
	}
}

void PlayerThread::Interval_Movement() {
	if (!isSpawned()) {return;}
	if (_TimeJobs.LastMovementSend + FC_INTERVAL_MOVEMENT <= getTicks()) { //Send new keep alive
		if (!(_Coordinates == _lastCoordinates)) {
			PlayerPoolEvent Event(_Coordinates,this);
			_pPoolMaster->Event(Event);
			_lastCoordinates = _Coordinates;
			_TimeJobs.LastMovementSend = getTicks();
		}else{
			if (_TimeJobs.LastMovementSend + 1000 <= getTicks()) {  //Workaound for the invisable player bug
				//Send a "still alive" event
				PlayerPoolEvent Event(_Coordinates,this); 
				_pPoolMaster->Event(Event);
				_TimeJobs.LastMovementSend = getTicks();
			}
		}
	}
}

void PlayerThread::spawnPlayer(int ID,EntityPlayer& rPlayer) {
	if (isEntitySpawned(ID)) {
		throw Poco::RuntimeException("Already spawned!");
	}
	if (ID == _iEntityID) {
		throw Poco::RuntimeException("Own EntityID can't be spawned!");
	}

	EntityListEntry Entry;

	Entry.EntityID = ID;
	Entry.Type = FC_ENTITY_PLAYER;
	Entry.oldPosition = rPlayer._Coordinates;

	_vSpawnedEntities.push_back(Entry);

	NetworkOut Out = _NetworkOutRoot.New();
	//Spawn player
	Out.addByte(0x14);
	Out.addInt(ID);
	Out.addString(rPlayer._sName);

	Out.addInt( (int) (rPlayer._Coordinates.X * 32.0));
	Out.addInt( (int) (rPlayer._Coordinates.Y * 32.0));
	Out.addInt( (int) (rPlayer._Coordinates.Z * 32.0));

	Out.addByte( (char) rPlayer._Coordinates.Yaw);
	Out.addByte( (char) rPlayer._Coordinates.Pitch);

	Out.addShort(rPlayer._aHeldItems[0].getItemID());

	Out.Finalize(FC_QUEUE_HIGH);

	//Spawn his equipment
	for (int x=0;x<=4;x++) {
		Out.addByte(0x05);
		Out.addInt(ID);
		Out.addShort(x);
		if (rPlayer._aHeldItems[x].getItemID() == 0) {
			Out.addShort(-1);
		}else{
			Out.addShort(rPlayer._aHeldItems[x].getItemID());
		}
		Out.addShort(0); //Damage/Metadata
		Out.Finalize(FC_QUEUE_HIGH);
	}
}

bool PlayerThread::isEntitySpawned(int ID) {
	if (_vSpawnedEntities.size() > 0) {
		for ( int x= 0;x<=_vSpawnedEntities.size()-1;x++) {
			if (_vSpawnedEntities[x].EntityID == ID){
				return true;
			}
		}
	}
	return false;
}

void PlayerThread::updateEntityPosition(int ID,EntityCoordinates Coordinates) {
	int id = -1;

	//search element
	if (_vSpawnedEntities.size()>0) {
		for (int x=0;x<=_vSpawnedEntities.size()-1;x++){
			if (_vSpawnedEntities[x].EntityID == ID){
				id = x;
			}
		}
	}else{
		throw Poco::RuntimeException("Not spawned");
	}
	if(id==-1) {
		throw Poco::RuntimeException("Not spawned");
	}


	if (_vSpawnedEntities[id].oldPosition == Coordinates) {
		return; //Coordinates are equal -> no update
	}

	NetworkOut Out = _NetworkOutRoot.New();

	double dX =  Coordinates.X - _vSpawnedEntities[id].oldPosition.X;
	double dY =  Coordinates.Y - _vSpawnedEntities[id].oldPosition.Y;
	double dZ =  Coordinates.Z - _vSpawnedEntities[id].oldPosition.Z;

	//cout<<"dX:"<<dX<<" dY:"<<dY<<" dZ:"<<dZ<<"\n";

	if(_vSpawnedEntities[id].oldPosition.LookEqual(Coordinates)) {	//Player just moved around and doesn't change camera 
		if (fabs(dX) <= 4.0 && fabs(dY) <= 4.0 && fabs(dZ) <= 4.0 ) {//Movement under 4 blocks
			//relative move
			Out.addByte(0x1F);
			Out.addInt(ID);
			Out.addByte(   (char) (dX*32.0) );
			Out.addByte(   (char) (dY*32.0) );
			Out.addByte(   (char) (dZ*32.0) );
			Out.Finalize(FC_QUEUE_HIGH);
			_vSpawnedEntities[id].oldPosition = Coordinates;
			return;
		}else {
			//Full update
		}
	}else{ //player moved camera
		if (_vSpawnedEntities[id].oldPosition.CoordinatesEqual(Coordinates)) { //Just moved camera
			Out.addByte(0x20);
			Out.addInt(ID);
			Out.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
			Out.addByte( (char) ((Coordinates.Pitch * 256.0F) / 360.0F) );
			Out.Finalize(FC_QUEUE_HIGH);
			_vSpawnedEntities[id].oldPosition = Coordinates;
			return;
		}
		if (fabs(dX) <= 4.0 && fabs(dY) <= 4.0 && fabs(dZ) <= 4.0 ) {//Movement under 4 blocks
			//relative move + camera
			Out.addByte(0x21);
			Out.addInt(ID);
			Out.addByte(   (char) (dX*32.0) );
			Out.addByte(   (char) (dY*32.0) );
			Out.addByte(   (char) (dZ*32.0) );
			Out.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
			Out.addByte( (char) ((Coordinates.Pitch * 256.0F) / 360.0F) );
			Out.Finalize(FC_QUEUE_HIGH);
			_vSpawnedEntities[id].oldPosition = Coordinates;
			return;
		}else {
			//Full update
		}
	}

	Out.addByte(0x22);

	Out.addInt(ID);

	Out.addInt( (int) (Coordinates.X * 32.0));
	Out.addInt( (int) (Coordinates.Y * 32.0));
	Out.addInt( (int) (Coordinates.Z * 32.0));

	Out.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
	Out.addByte( (char) ((Coordinates.Pitch * 256.0F) / 360.0F) );

	Out.Finalize(FC_QUEUE_HIGH);

	_vSpawnedEntities[id].oldPosition = Coordinates;
}

void PlayerThread::despawnEntity(int ID) {
	int id = -1;

	//search element
	if (_vSpawnedEntities.size()>0) {
		for (int x=0;x<=_vSpawnedEntities.size()-1;x++){
			if (_vSpawnedEntities[x].EntityID == ID){
				id = x;
			}
		}
	}else{
		throw Poco::RuntimeException("Not spawned");
	}
	if(id==-1) {
		throw Poco::RuntimeException("Not spawned");
	}

	_vSpawnedEntities.erase(_vSpawnedEntities.begin()+id);

	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x1D);
	Out.addInt(ID);
	Out.Finalize(FC_QUEUE_HIGH);
}

int PlayerThread::getEntityID() {
	return _iEntityID;
}

void PlayerThread::Packet18_Animation() {
	try {
		int iEID = _NetworkInRoot.readInt();
		char iAnimID = _NetworkInRoot.readByte();

		if (iEID != _iEntityID){
			Disconnect("You can't use other EntityID's as yours"); 
		}

		PlayerPoolEvent Event(iAnimID,this);
		_pPoolMaster->Event(Event);
	}catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::Packet19_EntityAction() {
	try {
		int iEID = _NetworkInRoot.readInt();
		char iActionID = _NetworkInRoot.readByte();

		if (iEID != _iEntityID){
			Disconnect("You can't use other EntityID's as yours"); 
		}

		switch(iActionID) {
		case FC_ACTION_CROUCH:
			_Flags.setCrouched(true);
			break;
		case FC_ACTION_UNCROUCH:
			_Flags.setCrouched(false);
			break;
		case FC_ACTION_STARTSPRINTING:
			_Flags.setSprinting(true);
			break;
		case FC_ACTION_STOPSPRINTING:
			_Flags.setSprinting(false);
			break;
		default:
			Disconnect("Unsupported action.");
			break;
		}

		PlayerPoolEvent Event(_Flags,this);
		_pPoolMaster->Event(Event);
	}catch(Poco::RuntimeException) {
		Disconnect(FC_LEAVE_OTHER);
	}
}

void PlayerThread::playAnimationOnEntity(int ID,char AnimID) {
	if (!isEntitySpawned(ID)) {
		throw Poco::RuntimeException("Not spawned!");
	}

	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x12);
	Out.addInt(ID);
	Out.addByte(AnimID);

	Out.Finalize(FC_QUEUE_HIGH);
}


void PlayerThread::updateEntityMetadata(int ID,EntityFlags Flags) {
	if (!isEntitySpawned(ID)) {
		throw Poco::RuntimeException("Not spawned!");
	}
	NetworkOut Out = _NetworkOutRoot.New();

	Out.addByte(0x28);
	Out.addInt(ID);


	char iMetadata = 0;
	//Flags 

	//Index 0 , general metadata
	Out.addByte(0); //Index = 0| Type=0
	iMetadata=0;
	iMetadata |= ((char)Flags.isOnFire()) & 1;
	iMetadata |= (((char)Flags.isCrouched()) & 1)<<1;
	iMetadata |= (((char)Flags.isRiding()) & 1 )<<2;
	iMetadata |= (((char)Flags.isSprinting()) & 1)<<3;
	iMetadata |= (((char)Flags.isEating()) & 1)<<4;
	Out.addByte(iMetadata);

	Out.addByte(127); //End of metadata
	Out.Finalize(FC_QUEUE_HIGH);
}

void PlayerThread::Interval_CalculateSpeed() {
	if (!isSpawned()) {return;}
	if (_TimeJobs.LastSpeedCalculation + FC_INTERVAL_CALCULATESPEED <= getTicks()) {
		double iTime = double(getTicks()) - double(_TimeJobs.LastSpeedCalculation);
		_TimeJobs.LastSpeedCalculation = getTicks();


		double iSpeed = (_dRunnedMeters * 60.0 * 60.0) / (iTime*3.6);
		if (iSpeed > 7.5) {
			//Disconnect("Speedhack");
		}
		_dRunnedMeters=0.0;
	}
}

PlayerInventory& PlayerThread::getInventory() {
	return _Inventory;
}

EntityFlags PlayerThread::getFlags() {
	return _Flags;
}

void PlayerThread::updateEntityEquipment(int ID,short Slot,ItemSlot Item) {
	if (!isEntitySpawned(ID)) {
		throw Poco::RuntimeException("Not spawned!");
	}
	if (Slot < 0 || Slot > 4) {
		throw Poco::RuntimeException("Invalid slot");
	}

	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x05);
	Out.addInt(ID);
	Out.addShort(Slot);
	if (Item.getItemID() == 0) {
		Out.addShort(-1);
	}else{
		Out.addShort(Item.getItemID());
	}
	Out.addShort(0); //Damage/Metadata
	Out.Finalize(FC_QUEUE_HIGH);
}

void PlayerThread::CheckPosition(bool fSynchronize) {
		if (_rWorld.isSuffocating(_Coordinates)) {
			char iHeight = -1;
			BlockCoordinates blockCoords = ChunkMath::toBlockCoords(_Coordinates);
			while(iHeight == -1) {
				iHeight = _rWorld.getFreeSpace(blockCoords.X,blockCoords.Z);
				if (iHeight == -1) {
					_Coordinates.X += 1.0;
					_Coordinates.Z += 1.0;
					blockCoords = ChunkMath::toBlockCoords(_Coordinates);
					continue;
				}
			}

			_Coordinates.Y = ((double) iHeight) + 1.0;
			if (fSynchronize) {sendClientPosition();}
		}
}

void PlayerThread::Interval_CheckPosition() {
	if (!isSpawned()) {return;}
	if (_TimeJobs.LastPositionCheck + FC_INTERVAL_CHECKPOSITION <= getTicks()) {
		_TimeJobs.LastPositionCheck = getTicks();
		CheckPosition();
	}
}

void PlayerThread::Packet15_PlayerBlockPlacement() {
	try {
		char Direction;
		ItemSlot Slot;
		BlockCoordinates ClientCoordinats = ChunkMath::toBlockCoords(_Coordinates);
		BlockCoordinates blockCoordinates;

		blockCoordinates.X = _NetworkInRoot.readInt();
		blockCoordinates.Y = _NetworkInRoot.readByte();
		blockCoordinates.Z = _NetworkInRoot.readInt();
		Direction = _NetworkInRoot.readByte();
		Slot.readFromNetwork(_NetworkInRoot);

		ItemSlot & InHand = _Inventory.getSelectedSlot();

		if (Slot.isEmpty()) {
			InHand.clear();
			return;
		}

		if (blockCoordinates.X==-1 && blockCoordinates.Y == -1 && blockCoordinates.Z == -1 && Direction == -1) { //Special action
			if (ItemInfoStorage::isBlock(_Inventory.getSelectedSlot().getItemID())) { //Special actions are not allowed with blocks
				sendEmptyBlock(blockCoordinates);
				return;
			}
			Disconnect("Not supported yet");
			return; 
		}else{ //Client wants to place a block
			if (! ItemInfoStorage::isBlock(_Inventory.getSelectedSlot().getItemID())) { //But it's a item...
				Disconnect("You tried to place a item");
				return;
			}
		}

		switch (Direction) {
		case 0:
			blockCoordinates.Y--;
			break;
		case 1:
			blockCoordinates.Y++;
			break;
		case 2:
			blockCoordinates.Z--;
			break;
		case 3:
			blockCoordinates.Z++;
			break;
		case 4:
			blockCoordinates.X--;
			break;
		case 5:
			blockCoordinates.X++;
			break;
		default:
			Disconnect("Illegal block direction: " + int(Direction));
			return;
		}

		/*
			Check blockCoordinates
		*/

		//Prevent: set a block into your body
		if ((ClientCoordinats.X == blockCoordinates.X && ClientCoordinats.Z == blockCoordinates.Z)  &&
			(ClientCoordinats.Y == blockCoordinates.Y || ClientCoordinats.Y+1==blockCoordinates.Y)) { 
			return;
		}

		//Prevent: set a block into other body
		if (_pPoolMaster->willHurtOther(blockCoordinates,this)) { 
			sendEmptyBlock(blockCoordinates);
			return;
		}

		//Prevent: set a block into other block 
		if (_rWorld.getBlock(blockCoordinates) != 0) { 
			return;
		}

		//Prevent: set a block above the map
		if (blockCoordinates.Y >= SettingsHandler::getWorldHeight()-2) { 
			sendEmptyBlock(blockCoordinates);
			return;
		}

		if (InHand.getStackSize()+1 < Slot.getStackSize()) {
			Disconnect("Inventory hack!");
			return;
		}else{
			InHand.setStackSize(Slot.getStackSize());
			if (Slot.getStackSize() == 0) {
				InHand.clear();
			}
		}

		/*
		* All test done. 
		*/
		NetworkOut Out = _NetworkOutRoot.New();
		Out.addByte(0x35);
		Out.addInt(blockCoordinates.X);
		Out.addByte(blockCoordinates.Y);
		Out.addInt(blockCoordinates.Z);
		Out.addByte((char)Slot.getItemID());
		Out.addByte(0);	
		Out.Finalize(FC_QUEUE_HIGH);

		//Append to map
		_rWorld.setBlock(blockCoordinates.X,blockCoordinates.Y,blockCoordinates.Z,(char)Slot.getItemID());
		
		//Event to player pool
		PlayerPoolEvent Event(blockCoordinates,(char)Slot.getItemID(),this);
		_pPoolMaster->Event(Event);
	} catch(Poco::RuntimeException& ex ) {
		cout<<"Exception cateched: "<<ex.message()<<"\n";
		Disconnect(FC_LEAVE_OTHER);
	}
}

int PlayerThread::getChunksInQueue() {
	return _NetworkOutRoot.getLowQueue().size();
}

void PlayerThread::spawnBlock(BlockCoordinates blockCoords,char Block) {
	if (!ItemInfoStorage::isRegistered(Block)) {
		throw Poco::RuntimeException("Block not registered");
	}

	ChunkCoordinates Coords;
	Coords = ChunkMath::toChunkCoords(blockCoords);

	if (!_ChunkProvider.isSpawned(Coords)) {
		return;
	}

	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x35);
	Out.addInt(blockCoords.X);
	Out.addByte((char)blockCoords.Y);
	Out.addInt(blockCoords.Z);
	Out.addByte(Block);
	Out.addByte(0);	
	Out.Finalize(FC_QUEUE_HIGH);
}

void PlayerThread::sendEmptyBlock(BlockCoordinates coords) {
	NetworkOut Out = _NetworkOutRoot.New();
	Out.addByte(0x35);
	Out.addInt(coords.X);
	Out.addByte((char)coords.Y);
	Out.addInt(coords.Z);
	Out.addByte(0);
	Out.addByte(0);		
	Out.Finalize(FC_QUEUE_HIGH);
}