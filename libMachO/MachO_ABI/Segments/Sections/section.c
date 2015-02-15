//----------------------------------------------------------------------------//
//|
//|             MachOKit - A Lightweight Mach-O Parsing Library
//|             section.c
//|
//|             D.V.
//|             Copyright (c) 2014-2015 D.V. All rights reserved.
//|
//| Permission is hereby granted, free of charge, to any person obtaining a
//| copy of this software and associated documentation files (the "Software"),
//| to deal in the Software without restriction, including without limitation
//| the rights to use, copy, modify, merge, publish, distribute, sublicense,
//| and/or sell copies of the Software, and to permit persons to whom the
//| Software is furnished to do so, subject to the following conditions:
//|
//| The above copyright notice and this permission notice shall be included
//| in all copies or substantial portions of the Software.
//|
//| THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//| OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//| MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//| IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//| CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//| TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//| SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//----------------------------------------------------------------------------//

#include "macho_abi_internal.h"

//----------------------------------------------------------------------------//
#pragma mark -  Classes
//----------------------------------------------------------------------------//

//|++++++++++++++++++++++++++++++++++++|//
static mk_context_t*
__mk_section_get_context(mk_type_ref self)
{ return mk_type_get_context( &((mk_section_t*)self)->section_segment ); }

const struct _mk_section_vtable _mk_section_class = {
    .base.super                 = &_mk_type_class,
    .base.name                  = "section",
    .base.get_context           = &__mk_section_get_context
};

//----------------------------------------------------------------------------//
#pragma mark -  Working With Sections
//----------------------------------------------------------------------------//

//|++++++++++++++++++++++++++++++++++++|//
mk_error_t
__mk_section_common_init(mk_segment_ref segment, struct section* s, mk_section_t* section)
{
    if (segment.type == NULL) return MK_EINVAL;
    if (s == NULL) return MK_EINVAL;
    if (section == NULL) return MK_EINVAL;
    
    mk_error_t err;
    
    mk_macho_ref image = mk_segment_get_macho(segment);
    mk_load_command_ref load_command = mk_segment_get_load_command(segment);
    bool is64 = (mk_load_command_id(load_command) == mk_load_command_segment_64_id());
    
    mk_vm_address_t vm_address;
    mk_vm_size_t vm_size;
    char sect_name[17] = {0x0};
    char seg_name[17] = {0x0};
    
    if (is64)
    {
        err = mk_load_command_segment_64_section_init(load_command, (struct section_64*)s, &section->section_64);
        if (err != MK_ESUCCESS)
            return err;
        
        vm_address = mk_load_command_segment_64_section_get_addr(&section->section_64);
        vm_size = mk_load_command_segment_64_section_get_size(&section->section_64);
        mk_load_command_segment_64_section_copy_name(&section->section_64, sect_name);
    }
    else
    {
        err = mk_load_command_segment_section_init(load_command, (struct section*)s, &section->section);
        if (err != MK_ESUCCESS)
            return err;
        
        vm_address = mk_load_command_segment_section_get_addr(&section->section);
        vm_size = mk_load_command_segment_section_get_size(&section->section);
        mk_load_command_segment_section_copy_name(&section->section, sect_name);
    }
    
    mk_segment_copy_name(segment, seg_name);
    
    // Slide the vmAddress
    {
        mk_vm_offset_t slide = mk_macho_get_slide(image);
        
        if ((err = mk_vm_address_apply_offset(vm_address, slide, &vm_address))) {
            _mkl_error(mk_type_get_context(load_command.type), "Arithmetic error %s while applying slide (%" MK_VM_PRIiOFFSET ") to vm_address (%" MK_VM_PRIxADDR ")", mk_error_string(err), slide, vm_address);
            return err;
        }
    }
    
    // Verify that this section is fully within it's segment's memory.
    if (mk_vm_range_contains_range(mk_memory_object_context_range(mk_segment_get_mobj(segment)), mk_vm_range_make(vm_address, vm_size), false) != MK_ESUCCESS) {
        _mkl_error(mk_type_get_context(load_command.type), "Section %s is not within segment %s", sect_name, seg_name);
        return err;
    }
    
    // Create a memory object for accessing this section.
    if ((err = mk_memory_map_init_object(mk_macho_get_memory_map(image), 0, vm_address, vm_size, false, &section->memory_object))) {
        _mkl_error(mk_type_get_context(load_command.type), "Failed to init memory object for section %s at (vm_address = %" MK_VM_PRIxADDR ", vm_size = %" MK_VM_PRIiSIZE "", sect_name, vm_address, vm_size);
        return err;
    }
    
    section->vtable = &_mk_section_class;
    section->section_segment = segment;
    
    return MK_ESUCCESS;
}

//|++++++++++++++++++++++++++++++++++++|//
mk_error_t mk_section_init_with_section(mk_segment_ref segment, struct section* s, mk_section_t* section)
{ return __mk_section_common_init(segment, s, section); }

//|++++++++++++++++++++++++++++++++++++|//
mk_error_t mk_section_init_with_section_64(mk_segment_ref segment, struct section_64* s, mk_section_t* section)
{ return __mk_section_common_init(segment, (struct section*)s, section); }

//|++++++++++++++++++++++++++++++++++++|//
void
mk_section_free(mk_section_ref section)
{
    mk_memory_map_free_object(mk_macho_get_memory_map(mk_section_get_macho(section)), &section.section->memory_object, NULL);
    section.section->vtable = NULL;
}

//|++++++++++++++++++++++++++++++++++++|//
mk_macho_ref mk_section_get_macho(mk_section_ref section)
{ return mk_segment_get_macho(section.section->section_segment); }

//|++++++++++++++++++++++++++++++++++++|//
mk_segment_ref mk_section_get_segment(mk_section_ref section)
{ return section.section->section_segment; }

//|++++++++++++++++++++++++++++++++++++|//
mk_memory_object_ref
mk_section_get_mobj(mk_section_ref section)
{
    mk_memory_object_ref ret;
    ret.memory_object = &section.section->memory_object;
    return ret;
}

//----------------------------------------------------------------------------//
#pragma mark -  Section Values
//----------------------------------------------------------------------------//

//|++++++++++++++++++++++++++++++++++++|//
size_t
mk_section_copy_section_name(mk_section_ref section, char output[16])
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_copy_name(&section.section->section_64, output);
    else
        return mk_load_command_segment_section_copy_name(&section.section->section, output);
}

//|++++++++++++++++++++++++++++++++++++|//
size_t
mk_section_copy_segment_name(mk_section_ref section, char output[16])
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_copy_segment_name(&section.section->section_64, output);
    else
        return mk_load_command_segment_section_copy_segment_name(&section.section->section, output);
}

//|++++++++++++++++++++++++++++++++++++|//
mk_vm_address_t
mk_section_get_vm_address(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_addr(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_addr(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
mk_vm_size_t
mk_section_get_vm_size(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_size(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_size(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
mk_vm_address_t
mk_section_get_vm_offset(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_offset(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_offset(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint32_t
mk_section_get_alignment(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_align(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_align(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint32_t
mk_section_get_relocations_offset(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_offset(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_offset(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint32_t
mk_section_get_number_relocations(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_nreloc(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_nreloc(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint8_t
mk_section_get_type(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_type(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_type(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint32_t
mk_section_get_attributes(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_attributes(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_attributes(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint32_t
mk_section_get_reserved1(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_reserved1(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_reserved1(&section.section->section);
}

//|++++++++++++++++++++++++++++++++++++|//
uint32_t
mk_section_get_reserved2(mk_section_ref section)
{
    if (mk_load_command_id(mk_segment_get_load_command(section.section->section_segment)) == mk_load_command_segment_64_id())
        return mk_load_command_segment_64_section_get_reserved2(&section.section->section_64);
    else
        return mk_load_command_segment_section_get_reserved2(&section.section->section);
}
