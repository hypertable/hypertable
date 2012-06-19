/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef HYPERTABLE_METALOGWRITER_H
#define HYPERTABLE_METALOGWRITER_H

#include "Common/Filesystem.h"
#include "Common/Mutex.h"
#include "Common/ReferenceCount.h"

#include <vector>

#include "MetaLogDefinition.h"
#include "MetaLogEntity.h"

namespace Hypertable {

  namespace MetaLog {

    class Writer : public ReferenceCount {
    public:
      Writer(FilesystemPtr &fs, DefinitionPtr &definition, const String &path,
             std::vector<EntityPtr> &initial_entities);
      ~Writer();
      void close();
      void record_state(Entity *entity);
      void record_state(std::vector<Entity *> &entities);
      void record_removal(Entity *entity);
      void record_removal(std::vector<Entity *> &entities);

      static bool skip_recover_entry;

    private:
      void write_header();
      void purge_old_log_files(std::vector<int32_t> &file_ids, size_t keep_count);
      void initialize_new_log_file(int32_t next_id);

      Mutex m_mutex;
      FilesystemPtr m_fs;
      DefinitionPtr m_definition;
      String  m_path;
      String  m_filename;
      int m_fd;
      int32_t m_backup_fileno;
      String  m_backup_path;
      String  m_backup_filename;
      int m_backup_fd;
      int m_offset;
    };
    typedef intrusive_ptr<Writer> WriterPtr;
    

  }

}

#endif // HYPERTABLE_METALOGWRITER_H
