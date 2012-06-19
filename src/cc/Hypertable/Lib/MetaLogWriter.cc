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
#include "Common/Compat.h"
#include "Common/Config.h"
#include "Common/FileUtils.h"
#include "Common/Path.h"
#include "Common/StringExt.h"

#include <algorithm>
#include <cassert>

#include <boost/algorithm/string.hpp>
#include <boost/shared_array.hpp>

#include "MetaLog.h"
#include "MetaLogWriter.h"

using namespace Hypertable;
using namespace Hypertable::MetaLog;

namespace {
  const int32_t DFS_BUFFER_SIZE = -1;
  const int64_t DFS_BLOCK_SIZE = -1;
}

bool Writer::skip_recover_entry = false;


Writer::Writer(FilesystemPtr &fs, DefinitionPtr &definition, const String &path,
               std::vector<EntityPtr> &initial_entities) :
  m_fs(fs), m_definition(definition), m_fd(-1), m_offset(0) {

  HT_EXPECT(Config::properties, Error::FAILED_EXPECTATION);

  // Setup DFS path name
  m_path = path;
  boost::trim_right_if(m_path, boost::is_any_of("/"));
  if (!m_fs->exists(m_path))
    m_fs->mkdirs(m_path);

  // Setup local backup path name
  Path data_dir = Config::properties->get_str("Hypertable.DataDirectory");
  m_backup_path = (data_dir /= String("/run/log_backup/") + String(m_definition->name()) + "/" +
                   String(m_definition->backup_label())).string();
  if (!FileUtils::exists(m_backup_path))
    FileUtils::mkdirs(m_backup_path);

  std::vector<int32_t> file_ids;
  int32_t next_id;

  scan_log_directory(m_fs, m_path, file_ids, &next_id);

  purge_old_log_files(file_ids, 10);

  // get replication
  int replication = Config::properties->get_i32("Hypertable.Metadata.Replication");

  // Open DFS file
  m_filename = m_path + "/" + next_id;
  m_fd = m_fs->create(m_filename, 0, DFS_BUFFER_SIZE, replication, DFS_BLOCK_SIZE);

  // Open backup file
  m_backup_filename = m_backup_path + "/" + next_id;
  m_backup_fd = ::open(m_backup_filename.c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0644);

  write_header();

  // Write existing entries
  foreach (EntityPtr &entity, initial_entities)
    record_state(entity.get());

  // Write "Recover" entity
  if (!skip_recover_entry) {
    EntityRecover recover_entity;
    record_state(&recover_entity);
  }

}

Writer::~Writer() {
  close();
}

void Writer::close() {
  ScopedLock lock(m_mutex);
  try {
    if (m_fd != -1) {
      m_fs->close(m_fd);
      m_fd = -1;
      ::close(m_backup_fd);
      m_backup_fd = -1;
    }
  }
  catch (Exception &e) {
    HT_THROW2F(e.code(), e, "Error closing metalog: %s (fd=%d)", m_filename.c_str(), m_fd);
  }

}


void Writer::purge_old_log_files(std::vector<int32_t> &file_ids, size_t keep_count) {
  ScopedLock lock(m_mutex);

  // reverse sort
  sort(file_ids.rbegin(), file_ids.rend());

  if (file_ids.size() > keep_count) {
    for (size_t i=keep_count; i< file_ids.size(); i++) {
      String tmp_name;

      // remove from DFS
      tmp_name = m_path + String("/") + file_ids[i];
      m_fs->remove(tmp_name);

      // remove local backup
      tmp_name = m_backup_path + String("/") + file_ids[i];
      if (FileUtils::exists(tmp_name))
	FileUtils::unlink(tmp_name);
    }
    file_ids.resize(keep_count);
  }
}


void Writer::write_header() {
  StaticBuffer buf(Header::LENGTH);
  uint8_t backup_buf[Header::LENGTH];
  Header header;

  assert(strlen(m_definition->name()) < sizeof(header.name));

  header.version = m_definition->version();
  memset(header.name, 0, sizeof(header.name));
  strcpy(header.name, m_definition->name());

  uint8_t *ptr = buf.base;

  header.encode(&ptr);

  assert((ptr-buf.base) == Header::LENGTH);
  memcpy(backup_buf, buf.base, Header::LENGTH);

  if (m_fs->append(m_fd, buf, Filesystem::O_FLUSH) != Header::LENGTH)
    HT_THROWF(Error::DFSBROKER_IO_ERROR, "Error writing %s "
              "metalog header to file: %s", m_definition->name(),
              m_filename.c_str());

  FileUtils::write(m_backup_fd, backup_buf, Header::LENGTH);
  m_offset += Header::LENGTH;
}


void Writer::record_state(Entity *entity) {
  ScopedLock lock(m_mutex);
  size_t length = EntityHeader::LENGTH + (entity->marked_for_removal() ? 0 : entity->encoded_length());
  StaticBuffer buf(length);
  boost::shared_array<uint8_t> backup_buf( new uint8_t [length] );
  uint8_t *ptr = buf.base;

  if (m_fd == -1)
    HT_THROWF(Error::CLOSED, "MetaLog '%s' has been closed", m_path.c_str());
  
  if (entity->marked_for_removal())
    entity->header.encode( &ptr );
  else
    entity->encode_entry( &ptr );

  HT_ASSERT((ptr-buf.base) == (ptrdiff_t)buf.size);
  memcpy(backup_buf.get(), buf.base, buf.size);

  m_fs->append(m_fd, buf, Filesystem::O_FLUSH);
  FileUtils::write(m_backup_fd, backup_buf.get(), buf.size);
  m_offset += buf.size;
}

void Writer::record_state(std::vector<Entity *> &entities) {
  ScopedLock lock(m_mutex);
  size_t length = 0;

  if (m_fd == -1)
    HT_THROWF(Error::CLOSED, "MetaLog '%s' has been closed", m_path.c_str());

  foreach (Entity *entity, entities)
    length += EntityHeader::LENGTH + (entity->marked_for_removal() ? 0 : entity->encoded_length());

  {
    StaticBuffer buf(length);
    boost::shared_array<uint8_t> backup_buf( new uint8_t [length] );
    uint8_t *ptr = buf.base;

    foreach (Entity *entity, entities) {
      if (entity->marked_for_removal())
        entity->header.encode( &ptr );
      else
        entity->encode_entry( &ptr );
    }

    HT_ASSERT((ptr-buf.base) == (ptrdiff_t)buf.size);
    memcpy(backup_buf.get(), buf.base, buf.size);

    m_fs->append(m_fd, buf, Filesystem::O_FLUSH);
    FileUtils::write(m_backup_fd, backup_buf.get(), buf.size);
    m_offset += buf.size;
  }
}


void Writer::record_removal(Entity *entity) {
  ScopedLock lock(m_mutex);
  StaticBuffer buf(EntityHeader::LENGTH);
  uint8_t backup_buf[EntityHeader::LENGTH];
  uint8_t *ptr = buf.base;

  if (m_fd == -1)
    HT_THROWF(Error::CLOSED, "MetaLog '%s' has been closed", m_path.c_str());

  entity->header.flags |= EntityHeader::FLAG_REMOVE;
  entity->header.length = 0;
  entity->header.checksum = 0;

  entity->header.encode( &ptr );

  HT_ASSERT((ptr-buf.base) == (ptrdiff_t)buf.size);
  memcpy(backup_buf, buf.base, buf.size);

  m_fs->append(m_fd, buf, Filesystem::O_FLUSH);
  FileUtils::write(m_backup_fd, backup_buf, buf.size);
  m_offset += buf.size;

}


void Writer::record_removal(std::vector<Entity *> &entities) {
  ScopedLock lock(m_mutex);
  size_t length = entities.size() * EntityHeader::LENGTH;

  if (m_fd == -1)
    HT_THROWF(Error::CLOSED, "MetaLog '%s' has been closed", m_path.c_str());

  {
    StaticBuffer buf(length);
    boost::shared_array<uint8_t> backup_buf( new uint8_t [length] );
    uint8_t *ptr = buf.base;

    for (size_t i=0; i<entities.size(); i++) {
      entities[i]->header.flags |= EntityHeader::FLAG_REMOVE;
      entities[i]->header.length = 0;
      entities[i]->header.checksum = 0;
      entities[i]->header.encode( &ptr );
    }

    HT_ASSERT((ptr-buf.base) == (ptrdiff_t)buf.size);
    memcpy(backup_buf.get(), buf.base, buf.size);

    m_fs->append(m_fd, buf, Filesystem::O_FLUSH);
    FileUtils::write(m_backup_fd, backup_buf.get(), buf.size);
    m_offset += buf.size;
  }

}
