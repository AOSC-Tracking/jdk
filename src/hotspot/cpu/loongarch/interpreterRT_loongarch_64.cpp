/*
 * Copyright (c) 2003, 2012, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2023, Loongson Technology. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "compiler/disassembler.hpp"
#include "interp_masm_loongarch.hpp"
#include "macroAssembler_loongarch.hpp"
#include "precompiled.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/universe.hpp"
#include "oops/method.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/icache.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/signature.hpp"

#define __ Disassembler::hook<MacroAssembler>(__FILE__, __LINE__, _masm)->

// Implementation of SignatureHandlerGenerator
InterpreterRuntime::SignatureHandlerGenerator::SignatureHandlerGenerator(
      const methodHandle& method, CodeBuffer* buffer) : NativeSignatureIterator(method) {
  _masm = new MacroAssembler(buffer);
  _num_int_args = (method->is_static() ? 1 : 0);
  _num_fp_args = 0;
  _stack_offset = 0;
}

void InterpreterRuntime::SignatureHandlerGenerator::move(int from_offset, int to_offset) {
  __ ld_d(temp(), from(), Interpreter::local_offset_in_bytes(from_offset));
  __ st_d(temp(), to(), to_offset * longSize);
}

void InterpreterRuntime::SignatureHandlerGenerator::box(int from_offset, int to_offset) {
  __ addi_d(temp(), from(),Interpreter::local_offset_in_bytes(from_offset) );
  __ ld_w(AT, from(), Interpreter::local_offset_in_bytes(from_offset) );

  __ maskeqz(temp(), temp(), AT);
  __ st_w(temp(), to(), to_offset * wordSize);
}

void InterpreterRuntime::SignatureHandlerGenerator::generate(uint64_t fingerprint) {
  // generate code to handle arguments
  iterate(fingerprint);
  // return result handler
  __ li(V0, AbstractInterpreter::result_handler(method()->result_type()));
  // return
  __ jr(RA);

  __ flush();
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_int() {
  if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
    __ ld_w(as_Register(++_num_int_args + A0->encoding()), from(), Interpreter::local_offset_in_bytes(offset()));
  } else {
    __ ld_w(AT, from(), Interpreter::local_offset_in_bytes(offset()));
    __ st_w(AT, to(), _stack_offset);
    _stack_offset += wordSize;
  }
}

// the jvm specifies that long type takes 2 stack spaces, so in do_long(), _offset += 2.
void InterpreterRuntime::SignatureHandlerGenerator::pass_long() {
  if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
    __ ld_d(as_Register(++_num_int_args + A0->encoding()), from(), Interpreter::local_offset_in_bytes(offset() + 1));
  } else {
    __ ld_d(AT, from(), Interpreter::local_offset_in_bytes(offset() + 1));
    __ st_d(AT, to(), _stack_offset);
    _stack_offset += wordSize;
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_object() {
  if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
    Register reg = as_Register(++_num_int_args + A0->encoding());
    if (_num_int_args == 1) {
      assert(offset() == 0, "argument register 1 can only be (non-null) receiver");
      __ addi_d(reg, from(), Interpreter::local_offset_in_bytes(offset()));
    } else {
      __ ld_d(reg, from(), Interpreter::local_offset_in_bytes(offset()));
      __ addi_d(AT, from(), Interpreter::local_offset_in_bytes(offset()));
      __ maskeqz(reg, AT, reg);
    }
  } else {
    __ ld_d(temp(), from(), Interpreter::local_offset_in_bytes(offset()));
    __ addi_d(AT, from(), Interpreter::local_offset_in_bytes(offset()));
    __ maskeqz(temp(), AT, temp());
    __ st_d(temp(), to(), _stack_offset);
    _stack_offset += wordSize;
  }
}

void InterpreterRuntime::SignatureHandlerGenerator::pass_float() {
  if (_num_fp_args < Argument::n_float_register_parameters_c) {
    __ fld_s(as_FloatRegister(_num_fp_args++), from(), Interpreter::local_offset_in_bytes(offset()));
  } else if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
    __ ld_w(as_Register(++_num_int_args + A0->encoding()), from(), Interpreter::local_offset_in_bytes(offset()));
  } else {
    __ ld_w(AT, from(), Interpreter::local_offset_in_bytes(offset()));
    __ st_w(AT, to(), _stack_offset);
    _stack_offset += wordSize;
  }
}

// the jvm specifies that double type takes 2 stack spaces, so in do_double(), _offset += 2.
void InterpreterRuntime::SignatureHandlerGenerator::pass_double() {
  if (_num_fp_args < Argument::n_float_register_parameters_c) {
    __ fld_d(as_FloatRegister(_num_fp_args++), from(), Interpreter::local_offset_in_bytes(offset() + 1));
  } else if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
    __ ld_d(as_Register(++_num_int_args + A0->encoding()), from(), Interpreter::local_offset_in_bytes(offset() + 1));
  } else {
    __ ld_d(AT, from(), Interpreter::local_offset_in_bytes(offset() + 1));
    __ st_d(AT, to(), _stack_offset);
    _stack_offset += wordSize;
  }
}


Register InterpreterRuntime::SignatureHandlerGenerator::from()       { return LVP; }
Register InterpreterRuntime::SignatureHandlerGenerator::to()         { return SP; }
Register InterpreterRuntime::SignatureHandlerGenerator::temp()       { return T8; }

// Implementation of SignatureHandlerLibrary

void SignatureHandlerLibrary::pd_set_handler(address handler) {}


class SlowSignatureHandler
  : public NativeSignatureIterator {
 private:
  address   _from;
  intptr_t* _to;
  intptr_t* _int_args;
  intptr_t* _fp_args;
  intptr_t* _fp_identifiers;
  unsigned int _num_int_args;
  unsigned int _num_fp_args;

  virtual void pass_int()
  {
    jint from_obj = *(jint *)(_from+Interpreter::local_offset_in_bytes(0));
    _from -= Interpreter::stackElementSize;

    if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
      *_int_args++ = from_obj;
      _num_int_args++;
    } else {
      *_to++ = from_obj;
    }
  }

  virtual void pass_long()
  {
    intptr_t from_obj = *(intptr_t*)(_from+Interpreter::local_offset_in_bytes(1));
    _from -= 2 * Interpreter::stackElementSize;

    if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
      *_int_args++ = from_obj;
      _num_int_args++;
    } else {
      *_to++ = from_obj;
    }
  }

  virtual void pass_object()
  {
    intptr_t *from_addr = (intptr_t*)(_from + Interpreter::local_offset_in_bytes(0));
    _from -= Interpreter::stackElementSize;

    if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
      *_int_args++ = (*from_addr == 0) ? (intptr_t)0 : (intptr_t)from_addr;
      _num_int_args++;
    } else {
      *_to++ = (*from_addr == 0) ? (intptr_t)0 : (intptr_t)from_addr;
    }
  }

  virtual void pass_float()
  {
    jint from_obj = *(jint *)(_from+Interpreter::local_offset_in_bytes(0));
    _from -= Interpreter::stackElementSize;

    if (_num_fp_args < Argument::n_float_register_parameters_c) {
      *_fp_args++ = from_obj;
      _num_fp_args++;
    } else if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
      *_int_args++ = from_obj;
      _num_int_args++;
    } else {
      *_to++ = from_obj;
    }
  }

  virtual void pass_double()
  {
    intptr_t from_obj = *(intptr_t*)(_from+Interpreter::local_offset_in_bytes(1));
    _from -= 2*Interpreter::stackElementSize;

    if (_num_fp_args < Argument::n_float_register_parameters_c) {
      *_fp_args++ = from_obj;
      *_fp_identifiers |= (1 << _num_fp_args); // mark as double
      _num_fp_args++;
    } else if (_num_int_args < Argument::n_int_register_parameters_c - 1) {
      *_int_args++ = from_obj;
      _num_int_args++;
    } else {
      *_to++ = from_obj;
    }
  }

 public:
  SlowSignatureHandler(methodHandle method, address from, intptr_t* to)
    : NativeSignatureIterator(method)
  {
    _from = from;
    _to   = to;

    // see TemplateInterpreterGenerator::generate_slow_signature_handler()
    _int_args = to - (method->is_static() ? 15 : 16);
    _fp_args =  to - 8;
    _fp_identifiers = to - 9;
    *(int*) _fp_identifiers = 0;
    _num_int_args = (method->is_static() ? 1 : 0);
    _num_fp_args = 0;
  }
};


JRT_ENTRY(address,
          InterpreterRuntime::slow_signature_handler(JavaThread* current,
                                                     Method* method,
                                                     intptr_t* from,
                                                     intptr_t* to))
  methodHandle m(current, (Method*)method);
  assert(m->is_native(), "sanity check");

  // handle arguments
  SlowSignatureHandler(m, (address)from, to).iterate(UCONST64(-1));

  // return result handler
  return Interpreter::result_handler(m->result_type());
JRT_END
