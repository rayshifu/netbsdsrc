/* $NetBSD: mdreloc.c,v 1.19 2024/07/22 23:10:35 riastradh Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2014-2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: mdreloc.c,v 1.19 2024/07/22 23:10:35 riastradh Exp $");
#endif /* not lint */

/*
 * AArch64 ELF relocations.
 *
 * References:
 *
 *	[AAELF64] ELF for the Arm 64-bit Architecture (AArch64),
 *	2022Q3.  Arm Ltd.
 *	https://github.com/ARM-software/abi-aa/blob/2982a9f3b512a5bfdc9e3fea5d3b298f9165c36b/aaelf64/aaelf64.rst
 *
 *	[TLSDESC] Glauber de Oliveira Costa and Alexandre Oliva,
 *	`Thread-Local Storage Access in Dynamic Libraries in the ARM
 *	Platform', 2006.
 *	https://www.fsfla.org/~lxoliva/writeups/TLS/paper-lk2006.pdf
 */

#include <sys/types.h>
#include <string.h>

#include "debug.h"
#include "rtld.h"

struct tls_data {
	size_t		td_tlsindex;
	Elf_Addr	td_tlsoffs;
};

void _rtld_bind_start(void);
void _rtld_relocate_nonplt_self(Elf_Dyn *, Elf_Addr);
Elf_Addr _rtld_bind(const Obj_Entry *, Elf_Word);
void *_rtld_tlsdesc_static(void *);
void *_rtld_tlsdesc_undef(void *);
void *_rtld_tlsdesc_dynamic(void *);

/*
 * AARCH64 PLT looks like this;
 *
 *	PLT HEADER <8 instructions>
 *	PLT ENTRY #0 <4 instructions>
 *	PLT ENTRY #1 <4 instructions>
 *	.
 *	.
 *	PLT ENTRY #n <4 instructions>
 *
 * PLT HEADER
 *	stp  x16, x30, [sp, #-16]!
 *	adrp x16, (GOT+16)
 *	ldr  x17, [x16, #PLT_GOT+0x10]
 *	add  x16, x16, #PLT_GOT+0x10
 *	br   x17
 *	nop
 *	nop
 *	nop
 *
 * PLT ENTRY #n
 *	adrp x16, PLTGOT + n * 8
 *	ldr  x17, [x16, PLTGOT + n * 8]
 *	add  x16, x16, :lo12:PLTGOT + n * 8
 *	br   x17
 */
void
_rtld_setup_pltgot(const Obj_Entry *obj)
{

	obj->pltgot[1] = (Elf_Addr) obj;
	obj->pltgot[2] = (Elf_Addr) &_rtld_bind_start;
}

static struct tls_data *
_rtld_tlsdesc_alloc(size_t tlsindex, Elf_Addr offs)
{
	struct tls_data *tlsdesc;

	tlsdesc = xmalloc(sizeof(*tlsdesc));
	tlsdesc->td_tlsindex = tlsindex;
	tlsdesc->td_tlsoffs = offs;

	return tlsdesc;
}

static void
_rtld_tlsdesc_fill(const Obj_Entry *obj, const Elf_Rela *rela, Elf_Addr *where, u_int flags)
{
	const Elf_Sym *def;
	const Obj_Entry *defobj;
	Elf_Addr offs = 0;
	unsigned long symnum = ELF_R_SYM(rela->r_info);

	if (symnum != 0) {
		def = _rtld_find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
		    flags);
		if (def == NULL)
			_rtld_die();
		if (def == &_rtld_sym_zero) {
			/* Weak undefined thread variable */
			where[0] = (Elf_Addr)_rtld_tlsdesc_undef;
			where[1] = rela->r_addend;

			rdbg(("TLSDESC %s (weak) in %s --> %p",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)where[1]));

			return;
		}
		offs = def->st_value;
	} else {
		defobj = obj;
	}
	offs += rela->r_addend;

	if (defobj->tls_static) {
		/* Variable is in initially allocated TLS segment */
		where[0] = (Elf_Addr)_rtld_tlsdesc_static;
		where[1] = defobj->tlsoffset + offs +
		    sizeof(struct tls_tcb);

		rdbg(("TLSDESC %s --> %p static",
		    obj->path, (void *)where[1]));
	} else {
		/* TLS offset is unknown at load time, use dynamic resolving */
		where[0] = (Elf_Addr)_rtld_tlsdesc_dynamic;
		where[1] = (Elf_Addr)_rtld_tlsdesc_alloc(defobj->tlsindex, offs);

		rdbg(("TLSDESC %s in %s --> %p dynamic (%zu, %p)",
		    obj->strtab + obj->symtab[symnum].st_name,
		    obj->path, (void *)where[1], defobj->tlsindex, (void *)offs));
	}
}

void
_rtld_relocate_nonplt_self(Elf_Dyn *dynp, Elf_Addr relocbase)
{
	const Elf_Rela *rela = 0, *relalim;
	Elf_Addr relasz = 0;
	Elf_Addr *where;

	for (; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_RELA:
			rela = (const Elf_Rela *)(relocbase + dynp->d_un.d_ptr);
			break;
		case DT_RELASZ:
			relasz = dynp->d_un.d_val;
			break;
		}
	}
	relalim = (const Elf_Rela *)((const uint8_t *)rela + relasz);
	for (; rela < relalim; rela++) {
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		*where += (Elf_Addr)relocbase;
	}
}

int
_rtld_relocate_nonplt_objects(Obj_Entry *obj)
{
	const Elf_Sym *def = NULL;
	const Obj_Entry *defobj = NULL;
	unsigned long last_symnum = ULONG_MAX;

	for (const Elf_Rela *rela = obj->rela; rela < obj->relalim; rela++) {
		Elf_Addr        *where;
		Elf_Addr	tmp;
		unsigned long	symnum = ULONG_MAX;

		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);

		switch (ELF_R_TYPE(rela->r_info)) {
		case R_TYPE(ABS64):	/* word S + A */
		case R_TYPE(GLOB_DAT):	/* word S + A */
		case R_TLS_TYPE(TLS_DTPREL):
		case R_TLS_TYPE(TLS_DTPMOD):
		case R_TLS_TYPE(TLS_TPREL):
			symnum = ELF_R_SYM(rela->r_info);
			if (last_symnum != symnum) {
				last_symnum = symnum;
				def = _rtld_find_symdef(symnum, obj, &defobj,
				    false);
				if (def == NULL)
					return -1;
			}

		default:
			break;
		}

		switch (ELF_R_TYPE(rela->r_info)) {
		case R_TYPE(NONE):
			break;

		case R_TYPE(ABS64):	/* word S + A */
		case R_TYPE(GLOB_DAT):	/* word S + A */
			tmp = (Elf_Addr)defobj->relocbase + def->st_value +
			    rela->r_addend;
			if (*where != tmp)
				*where = tmp;
			rdbg(("ABS64/GLOB_DAT %s in %s --> %p @ %p in %s",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)tmp, where, defobj->path));
			break;

		case R_TYPE(IRELATIVE):
			/* IFUNC relocations are handled in _rtld_call_ifunc */
			if (obj->ifunc_remaining_nonplt == 0)
				obj->ifunc_remaining_nonplt = obj->relalim - rela;
			rdbg(("IRELATIVE in %s, %zx", obj->path,
			    obj->ifunc_remaining_nonplt));
			/* FALLTHROUGH */

		case R_TYPE(RELATIVE):	/* word B + A */
			*where = (Elf_Addr)(obj->relocbase + rela->r_addend);
			rdbg(("RELATIVE in %s --> %p", obj->path,
			    (void *)*where));
			break;

		case R_TYPE(COPY):
			/*
			 * These are deferred until all other relocations have
			 * been done.  All we do here is make sure that the
			 * COPY relocation is not in a shared library.  They
			 * are allowed only in executable files.
			 */
			if (obj->isdynamic) {
				_rtld_error(
			"%s: Unexpected R_COPY relocation in shared library",
				    obj->path);
				return -1;
			}
			rdbg(("COPY (avoid in main)"));
			break;

		case R_TYPE(TLSDESC):
			_rtld_tlsdesc_fill(obj, rela, where, 0);
			break;

		case R_TLS_TYPE(TLS_DTPREL):
			*where = (Elf_Addr)(def->st_value + rela->r_addend);

			rdbg(("TLS_DTPREL %s in %s --> %p",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)*where));
			break;

		case R_TLS_TYPE(TLS_DTPMOD):
			*where = (Elf_Addr)(defobj->tlsindex);

			rdbg(("TLS_DTPMOD %s in %s --> %p",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)*where));
			break;

		case R_TLS_TYPE(TLS_TPREL):
			if (!defobj->tls_static &&
			    _rtld_tls_offset_allocate(__UNCONST(defobj)))
				return -1;

			*where = (Elf_Addr)(def->st_value + defobj->tlsoffset +
			    rela->r_addend + sizeof(struct tls_tcb));

			rdbg(("TLS_TPREL %s in %s --> %p in %s",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)*where, defobj->path));
			break;

		default:
			rdbg(("sym = %lu, type = %lu, offset = %p, "
			    "addend = %p, contents = %p",
			    (u_long)ELF_R_SYM(rela->r_info),
			    (u_long)ELF_R_TYPE(rela->r_info),
			    (void *)rela->r_offset, (void *)rela->r_addend,
			    (void *)*where));
			_rtld_error("%s: Unsupported relocation type %ld "
			    "in non-PLT relocations",
			    obj->path, (u_long) ELF_R_TYPE(rela->r_info));
			return -1;
		}
	}
	return 0;
}

int
_rtld_relocate_plt_lazy(Obj_Entry *obj)
{

	if (!obj->relocbase)
		return 0;

	for (const Elf_Rela *rela = obj->pltrela; rela < obj->pltrelalim; rela++) {
		Elf_Addr *where = (Elf_Addr *)(obj->relocbase + rela->r_offset);

		assert(ELF_R_TYPE(rela->r_info) == R_TYPE(JUMP_SLOT) ||
		       ELF_R_TYPE(rela->r_info) == R_TYPE(TLSDESC) ||
		       ELF_R_TYPE(rela->r_info) == R_TYPE(IRELATIVE));

		switch (ELF_R_TYPE(rela->r_info)) {
		case R_TYPE(JUMP_SLOT):
			/* Just relocate the GOT slots pointing into the PLT */
			*where += (Elf_Addr)obj->relocbase;
			rdbg(("fixup !main in %s --> %p", obj->path, (void *)*where));
			break;
		case R_TYPE(TLSDESC):
			_rtld_tlsdesc_fill(obj, rela, where, SYMLOOK_IN_PLT);
			break;
		case R_TYPE(IRELATIVE):
			obj->ifunc_remaining = obj->pltrelalim - rela;
			break;
		}
	}

	return 0;
}

void
_rtld_call_ifunc(Obj_Entry *obj, sigset_t *mask, u_int cur_objgen)
{
	const Elf_Rela *rela;
	Elf_Addr *where, target;

	while (obj->ifunc_remaining > 0 && _rtld_objgen == cur_objgen) {
		rela = obj->pltrelalim - obj->ifunc_remaining;
		--obj->ifunc_remaining;
		if (ELF_R_TYPE(rela->r_info) == R_TYPE(IRELATIVE)) {
			where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
			target = (Elf_Addr)(obj->relocbase + rela->r_addend);
			_rtld_exclusive_exit(mask);
			target = _rtld_resolve_ifunc2(obj, target);
			_rtld_exclusive_enter(mask);
			if (*where != target)
				*where = target;
		}
	}
}

static int
_rtld_relocate_plt_object(const Obj_Entry *obj, const Elf_Rela *rela,
	Elf_Addr *tp)
{
	Elf_Addr *where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
	Elf_Addr new_value;
	const Elf_Sym  *def;
	const Obj_Entry *defobj;

	switch (ELF_R_TYPE(rela->r_info)) {
	case R_TYPE(JUMP_SLOT):
		def = _rtld_find_plt_symdef(ELF_R_SYM(rela->r_info), obj,
		    &defobj, tp != NULL);
		if (__predict_false(def == NULL))
			return -1;
		if (__predict_false(def == &_rtld_sym_zero))
			return 0;

		if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC) {
			if (tp == NULL)
				return 0;
			new_value = _rtld_resolve_ifunc(defobj, def);
		} else {
			new_value = (Elf_Addr)(defobj->relocbase +
			     def->st_value);
		}
		rdbg(("bind now/fixup in %s --> old=%p new=%p",
		    defobj->strtab + def->st_name, (void *)*where,
		    (void *)new_value));
		if (*where != new_value)
			*where = new_value;
		if (tp)
			*tp = new_value;
		break;
	case R_TYPE(TLSDESC):
		_rtld_tlsdesc_fill(obj, rela, where, SYMLOOK_IN_PLT);
		break;
	}

	return 0;
}

Elf_Addr
_rtld_bind(const Obj_Entry *obj, Elf_Word relaidx)
{
	const Elf_Rela *rela = obj->pltrela + relaidx;
	Elf_Addr new_value = 0;

	_rtld_shared_enter();
	int err = _rtld_relocate_plt_object(obj, rela, &new_value);
	if (err)
		_rtld_die();
	_rtld_shared_exit();

	return new_value;
}
int
_rtld_relocate_plt_objects(const Obj_Entry *obj)
{
	const Elf_Rela *rela;
	int err = 0;

	for (rela = obj->pltrela; rela < obj->pltrelalim; rela++) {
		err = _rtld_relocate_plt_object(obj, rela, NULL);
		if (err)
			break;
	}

	return err;
}
