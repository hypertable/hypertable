

file(MAKE_DIRECTORY ${HYPERTABLE_BINARY_DIR}/src/java/hypertable-common/src/main/java)
add_custom_target(ThriftGenJava ALL)
add_custom_command(
  TARGET 	ThriftGenJava
  COMMAND   thrift
  ARGS      -r --gen java -o ${HYPERTABLE_BINARY_DIR}/src/java/hypertable-common/src/main/java
            ${ThriftBroker_IDL_DIR}/Hql.thrift
  DEPENDS   ${ThriftBroker_IDL_DIR}/Client.thrift
            ${ThriftBroker_IDL_DIR}/Hql.thrift
  COMMENT   "thrift2java"
)


if (PREVIOUS_JAVA_GEN)  # expected dest file velidation

set(ThriftGenJava_DIR ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/src/main/java/org/hypertable/thriftgen)
  set(ThriftGenJava_SRCS
      ${ThriftGenJava_DIR}/AccessGroupOptions.java
      ${ThriftGenJava_DIR}/AccessGroupSpec.java
      ${ThriftGenJava_DIR}/CellInterval.java
      ${ThriftGenJava_DIR}/Cell.java
      ${ThriftGenJava_DIR}/ClientException.java
      ${ThriftGenJava_DIR}/ClientService.java
      ${ThriftGenJava_DIR}/ColumnFamilyOptions.java
      ${ThriftGenJava_DIR}/ColumnFamilySpec.java
      ${ThriftGenJava_DIR}/ColumnPredicate.java
      ${ThriftGenJava_DIR}/ColumnPredicateOperation.java
      ${ThriftGenJava_DIR}/HqlResult2.java
      ${ThriftGenJava_DIR}/HqlResultAsArrays.java
      ${ThriftGenJava_DIR}/HqlResult.java
      ${ThriftGenJava_DIR}/HqlService.java
      ${ThriftGenJava_DIR}/KeyFlag.java
      ${ThriftGenJava_DIR}/Key.java
      ${ThriftGenJava_DIR}/MutateSpec.java
      ${ThriftGenJava_DIR}/MutatorFlag.java
      ${ThriftGenJava_DIR}/NamespaceListing.java
      ${ThriftGenJava_DIR}/ResultAsArrays.java
      ${ThriftGenJava_DIR}/Result.java
      ${ThriftGenJava_DIR}/ResultSerialized.java
      ${ThriftGenJava_DIR}/RowInterval.java
      ${ThriftGenJava_DIR}/ScanSpec.java
      ${ThriftGenJava_DIR}/Schema.java
      ${ThriftGenJava_DIR}/TableSplit.java
  )

  add_custom_command(
    OUTPUT    ${ThriftGenJava_SRCS}
    COMMAND   thrift
    ARGS      -r -gen java -out ${HYPERTABLE_BINARY_DIR}/java/hypertable-common/src/main/java
              ${ThriftBroker_IDL_DIR}/Hql.thrift
    DEPENDS   ${ThriftBroker_IDL_DIR}/Client.thrift
              ${ThriftBroker_IDL_DIR}/Hql.thrift
    COMMENT   "thrift2java"
  )
endif ()


if (JAVAC_VERSION)
  add_test(ThriftClient-java
           bash -c "${INSTALL_DIR}/bin/ht-set-hadoop-distro.sh cdh5; ${INSTALL_DIR}/bin/ht-java-run.sh org.hypertable.thrift.BasicClientTest")
endif ()
