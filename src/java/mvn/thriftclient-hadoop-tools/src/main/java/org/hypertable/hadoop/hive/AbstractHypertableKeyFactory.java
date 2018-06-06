/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.hypertable.hadoop.hive;

import org.apache.hadoop.hive.ql.plan.ExprNodeDesc;
import org.apache.hadoop.hive.ql.plan.TableDesc;
import org.apache.hadoop.hive.serde2.Deserializer;
import org.apache.hadoop.hive.serde2.SerDeException;
import org.apache.hadoop.mapred.JobConf;

import java.io.IOException;
import java.util.Properties;

public abstract class AbstractHypertableKeyFactory implements HypertableKeyFactory {

  protected HypertableSerDeParameters hypertableParams;
  protected ColumnMappings.ColumnMapping keyMapping;
  protected Properties properties;

  @Override
  public void init(HypertableSerDeParameters hypertableParam, Properties properties) throws SerDeException {
    this.hypertableParams = hypertableParam;
    this.keyMapping = hypertableParam.getKeyColumnMapping();
    this.properties = properties;
  }

  @Override
  public void configureJobConf(TableDesc tableDesc, JobConf jobConf) throws IOException {
    jobConf.set("tmpjars", getClass().getProtectionDomain().getCodeSource().getLocation().getPath());
  }

  @Override
  public DecomposedPredicate decomposePredicate(JobConf jobConf, Deserializer deserializer, ExprNodeDesc predicate) {
    return HypertableStorageHandler.decomposePredicate(jobConf, (HypertableSerDe) deserializer, predicate);
  }
}
