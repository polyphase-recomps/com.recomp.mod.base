/**
 * @file ModBaseApi.h
 * @brief Export macro of com.recomp.mod.base.
 *
 * In the editor every addon is its own DLL: the base exports, the runtime addons that
 * depend on it (com.recomp.ps1, com.recomp.gcn, ...) import. Shipped builds compile all
 * addons into one program, so the macro is empty there.
 */
#pragma once

#if EDITOR && defined(_WIN32)
#if defined(MODBASE_EXPORTS)
#define MODBASE_API __declspec(dllexport)
#else
#define MODBASE_API __declspec(dllimport)
#endif
#else
#define MODBASE_API
#endif
