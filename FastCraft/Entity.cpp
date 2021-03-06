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
#include "Entity.h"
#include "MinecraftServer.h"
#include "World.h"
#include "NetworkOut.h"
#include "Constants.h"
#include <cmath>
#include "FCException.h"

Entity::Entity(MinecraftServer* pServer,World* pWorld,bool fGRabNewEID) {
	if (pServer == NULL || _pWorld == NULL) {throw FCException("Nullpointer are not allowed");}
	
	if(fGRabNewEID) { 
		_iEntityID = pServer->generateID();
	}else{
		_iEntityID = -1;
	}
	_iCreation = pServer->getTimestamp();
	_pWorld = pWorld;
	_pMCServer = pServer;
}

Entity::~Entity(){
}

int Entity::getEntityID() {
	return _iEntityID;
}

World* Entity::getWorld() {
	return _pWorld;
}

Tick Entity::getLiveTime() {
	return _pMCServer->getTimestamp() - _iCreation;
}


bool Entity::isAlive() {
	return false;
}

float Entity::getHeight() {
	return 0.0F;
}

float Entity::getWidth() {
	return 0.0F;
}

string Entity::getName() {
	return string("");
}

void Entity::spawn(NetworkOut& rOut) {
	try {
		rOut.addByte(0x1E);
		rOut.addInt(_iEntityID);
		rOut.Finalize(FC_QUEUE_HIGH);
	}catch (FCException& ex) {
		ex.rethrow();
	}
}


void Entity::syncCoordinates(NetworkOut& rOut,EntityCoordinates& lastCoordinates) {
	double dX =  Coordinates.X - lastCoordinates.X;
	double dY =  Coordinates.Y - lastCoordinates.Y;
	double dZ =  Coordinates.Z - lastCoordinates.Z;

	/*if (lastCoordinates.HeadYaw != Coordinates.HeadYaw) {
		rOut.addByte(0x23);//Head move
		rOut.addInt(_iEntityID);
		rOut.addInt((char) ((Coordinates.Pitch * 256.0F) / 360.0F));
		rOut.Finalize(FC_QUEUE_HIGH);
	}*/
	try {
		if(lastCoordinates.LookEqual(Coordinates)) {	//Player just moved around and doesn't change camera 
			if (fabs(dX) <= 4.0 && fabs(dY) <= 4.0 && fabs(dZ) <= 4.0 ) {//Movement under 4 blocks
				rOut.addByte(0x1F);//relative move
				rOut.addInt(_iEntityID);
				rOut.addByte(   (char) (dX*32.0) );
				rOut.addByte(   (char) (dY*32.0) );
				rOut.addByte(   (char) (dZ*32.0) );
				rOut.Finalize(FC_QUEUE_HIGH);
				return;
			}
			/* else: full update */
		}else{ //player moved camera
			if (lastCoordinates.CoordinatesEqual(Coordinates)) { //Just moved camera
				rOut.addByte(0x20); //Entity Look
				rOut.addInt(_iEntityID);
				rOut.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
				rOut.addByte( (char) ((Coordinates.Pitch * 256.0F) / 360.0F) );
				rOut.Finalize(FC_QUEUE_HIGH);


				rOut.addByte(0x23); //Entity Look
				rOut.addInt(_iEntityID);
				rOut.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
				rOut.Finalize(FC_QUEUE_HIGH);
				return;
			}
			if (fabs(dX) <= 4.0 && fabs(dY) <= 4.0 && fabs(dZ) <= 4.0 ) {//Movement under 4 blocks
				rOut.addByte(0x21);//relative move + camera
				rOut.addInt(_iEntityID);
				rOut.addByte((char) (dX*32.0) );
				rOut.addByte((char) (dY*32.0) );
				rOut.addByte((char) (dZ*32.0) );
				rOut.addByte((char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
				rOut.addByte((char) ((Coordinates.Pitch * 256.0F) / 360.0F) );
				rOut.Finalize(FC_QUEUE_HIGH);

				rOut.addByte(0x23); //Entity Look
				rOut.addInt(_iEntityID);
				rOut.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
				rOut.Finalize(FC_QUEUE_HIGH);
				return;
			}
			/* else: full update */
		}

		//Full update
		rOut.addByte(0x22);
		rOut.addInt(_iEntityID);

		rOut.addInt( (int) (Coordinates.X * 32.0));
		rOut.addInt( (int) (Coordinates.Y * 32.0));
		rOut.addInt( (int) (Coordinates.Z * 32.0));

		rOut.addByte( (char) ((Coordinates.Yaw * 256.0F) / 360.0F) );
		rOut.addByte( (char) ((Coordinates.Pitch * 256.0F) / 360.0F) );
		//rOut.addByte( (char) ((Coordinates.HeadYaw * 256.0F) / 360.0F) );

		rOut.Finalize(FC_QUEUE_HIGH);
	}catch (FCException& ex) {
		ex.rethrow();
	}
}

char Entity::getBaseType() {
	return Constants::get("/Entity/BaseType/Entity");
}

MinecraftServer* Entity::getMinecraftServer(){
	return _pMCServer;
}
