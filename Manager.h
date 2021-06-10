#pragma once
#include "Singleton.h"

class Manager : public Singleton<Manager>
{
private:
public:
	void Init();
	void Uninit();
	void Update();
	void Draw();

};