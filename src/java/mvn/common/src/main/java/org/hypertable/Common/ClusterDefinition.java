/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

package org.hypertable.Common;

import java.io.File;
import java.text.ParseException;
import java.util.HashMap;
import java.util.Map;
import java.util.Vector;

import org.hypertable.Common.ClusterDefinitionFile.Token;
import org.hypertable.Common.ClusterDefinitionFile.Tokenizer;

/**
 * Cluster definition.
 * This class provides convenient access to information contained within a
 * cluster definition file.
 */
public class ClusterDefinition {

  /**
   * Role membership information.
   */
  public class Role {
    /** Role name */
    public String name;
    /** Role members */
    public Vector<String> members;
    /** Generation number for detecting membership changes */
    public long generation;
  };

  /** Constructor.
   * @param fname Name of cluster definition file
   */
  public ClusterDefinition(String fname) {
    mFile = new File(fname);
  }

  /** Gets role membership information.
   * This method first loads the cluster definition file, if it hasn't been
   * modified since the last time it was loaded.  It then returns the membership
   * information for role <code>name</code>, using the last modification time of
   * the cluster definition file as the generation number.
   * @param name Role name
   */
  public synchronized Role getRole(String name) {
    Role role = new Role();
    role.name = name;
    loadFile();
    role.generation = mLastMtime;
    role.members = mRoles.get(name);
    return role;
  }

  /**
   * Loads the cluster definition file.
   * If the cluster definition file does not exist, the {@link #mRoles} member
   * is cleared and {@link #mLastMtime} is set to 0.  Otherwise, the file
   * modification time is read and compared with {@link #mLastMtime} and if
   * greater, the file is re-loaded.
   * time it was loaded.
   */
  private void loadFile() {

    try {

      long mtime = mFile.lastModified();

      if (mtime == 0) {
        mRoles.clear();
        mLastMtime = 0;
        return;
      }

      if (mtime > mLastMtime) {
        mRoles.clear();
        Tokenizer tokenizer = new Tokenizer(mFile.getPath());
        Token token;
        while ((token = tokenizer.next()) != null) {
          if (token.type() == Token.Type.ROLE)
            addRole(token.text());
        }
        mLastMtime = mtime;
      }
    }
    catch (Exception e) {
      e.printStackTrace();
      java.lang.System.exit(1);
    }
  }

  /** Adds role definition.
   * This method parses the role definition given in <code>text</code> and if it
   * is non-empty, it expands the host specification into a vector of host names
   * and adds an entry to {@link #mRoles}.
   * @param text Role definition text
   */
  private void addRole(String text) throws ParseException {
    String [] strs = text.trim().split("\\s+", 3);
    assert strs[0].equals("role:");
    if (strs.length == 3)
      mRoles.put(strs[1], new HostSpecification(strs[2]).expand());
  }

  /** Last modification time of cluster definition file */
  long mLastMtime;

  /** Cluster definition file */
  File mFile;

  /** Map from role names to members */
  Map<String, Vector<String>> mRoles = new HashMap<String, Vector<String>>();

}
