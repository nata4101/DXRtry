#include "stdafx.h"
#include "Manager.h"
#include "Renderer.h"

Renderer* renderer;

void Manager::Init()
{
	renderer = Renderer::GetInstance();
	renderer->Init();
}

void Manager::Uninit()
{
}

void Manager::Update()
{
}

void Manager::Draw()
{
}
