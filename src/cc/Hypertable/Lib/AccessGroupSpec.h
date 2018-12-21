/* -*- c++ -*-
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

/// @file
/// Declarations for AccessGroupSpec.
/// This file contains type declarations for AccessGroupSpec, a class
/// representing an access group specification.

#ifndef Hypertable_Lib_AccessGroupSpec_h
#define Hypertable_Lib_AccessGroupSpec_h

#include <Hypertable/Lib/ColumnFamilySpec.h>

#include <Common/Properties.h>

#include <bitset>
#include <unordered_map>
#include <vector>

namespace Hypertable {

  /// @addtogroup libHypertable
  /// @{

  /// Enumeration for bloom filter modes
  enum BloomFilterMode {
    /// Disable
    BLOOM_FILTER_DISABLED,
    /// Rows only
    BLOOM_FILTER_ROWS,
    /// Rows plus columns
    BLOOM_FILTER_ROWS_COLS
  };
  /*
  // Configuration Property
  class ConfBloomFilterMode : public Property::EnumExt {
    public:
      enum enums BloomFilterMode;
      bool valid(String &name){
        return opts.find(name) != opts.end();
      }
      String str(enums num){
        return opts[num];
      }
      enums value(String &name){
        return std::distance(opts.begin(),
                             std::find(opts.begin(), opts.end(), name));
      }
    private:
      // vector/map to enum index on strings/mode aliases
      std::vector<String> opts = {"none","rows","rows+cols"};
  };
  */
  /// Specification for access group options.
  class AccessGroupOptions {
  public:

    /// Enumeration for <i>isset</i> bits
    enum {
      /// <i>replication</i> bit
      REPLICATION,
      /// <i>blocksize</i> bit
      BLOCKSIZE,
      /// <i>compressor</i> bit
      COMPRESSOR,
      /// <i>bloom filter</i> bit
      BLOOMFILTER,
      /// <i>in memory</i> bit
      IN_MEMORY,
      /// Total bit count
      MAX
    };

    /// Sets <i>replication</i> option.
    /// Sets the REPLICATON bit of #m_isset and sets #m_replication to
    /// <code>replication</code>.
    /// @param replication New value for <i>replication</i> option
    void set_replication(int16_t replication);

    /// Gets <i>replication</i> option.
    /// @return <i>replication</i> option.
    int16_t get_replication() const { return m_replication; }

    /// Checks if <i>replication</i> option is set.
    /// This method returns the value of the REPLICATION bit of #m_isset.
    /// @return <i>true</i> if <i>replication</i> option is set, <i>false</i>
    /// otherwise.
    bool is_set_replication() const;

    /// Sets <i>blocksize</i> option.
    /// Sets the BLOCKSIZE bit of #m_isset and sets #m_blocksize to
    /// <code>blocksize</code>.
    /// @param blocksize New value for <i>blocksize</i> option
    void set_blocksize(int32_t blocksize);

    /// Gets <i>blocksize</i> option.
    /// @return <i>blocksize</i> option.
    int32_t get_blocksize() const { return m_blocksize; }

    /// Checks if <i>blocksize</i> option is set.
    /// This method returns the value of the BLOCKSIZE bit of #m_isset.
    /// @return <i>true</i> if <i>blocksize</i> option is set, <i>false</i>
    /// otherwise.
    bool is_set_blocksize() const;

    /// Sets <i>compressor</i> option.
    /// Sets the COMPRESSOR bit of #m_isset, validates the specification given
    /// in the <code>compressor</code> argument, and if it is valid, sets
    /// #m_compressor to <code>compressor</code>.  The following compressor
    /// specifications are valid:
    /// <pre>
    ///   bmz [--fp-len &lt;int&gt;] [--offset &lt;int&gt;]
    ///   lzo
    ///   quicklz
    ///   zlib [--best|--9|--normal]
    ///   snappy
    ///   zstd [|--ultra|--20|--best|--9|--normal]
    ///   none
    /// </pre>
    /// @param compressor Compressor specification
    /// @throws Exception with code set to Error::SCHEMA_PARSE_ERROR
    /// if compressor specification is invalid
    void set_compressor(const std::string &compressor);

    /// Gets <i>compressor</i> option.
    /// @return <i>compressor</i> option.
    const std::string &get_compressor() const { return m_compressor; }

    /// Checks if <i>compressor</i> option is set.
    /// This method returns the value of the COMPRESSOR bit of #m_isset.
    /// @return <i>true</i> if <i>compressor</i> option is set, <i>false</i>
    /// otherwise.
    bool is_set_compressor() const;

    /// Sets <i>bloom filter</i> option.
    /// Sets the BLOOMFILTER bit of #m_isset, validates the specification given
    /// in the <code>bloomfilter</code> argument, and if it is valid, sets
    /// #m_bloomfilter to <code>bloomfilter</code>.  The following bloom filter
    /// specifications are valid:
    /// <pre>
    /// mode:
    ///   rows [options]
    ///   rows+cols [options]
    ///   none
    ///
    /// options:
    ///   --bits-per-item &lt;float&gt;
    ///   --num-hashes &lt;int&gt;
    ///   --false-positive &lt;float&gt;
    ///   --max-approx-items &lt;int&gt;
    /// </pre>
    /// @param bloomfilter Bloom filter specification
    /// @throws Exception with code set to Error::SCHEMA_PARSE_ERROR
    /// if bloom filter specification is invalid
    void set_bloom_filter(const std::string &bloomfilter);

    /// Gets <i>bloom filter</i> option.
    /// @return <i>bloom filter</i> option.
    const std::string &get_bloom_filter() const { return m_bloomfilter; }

    /// Checks if <i>bloom filter</i> option is set.
    /// This method returns the value of the BLOOMFILTER bit of #m_isset.
    /// @return <i>true</i> if <i>bloom filter</i> option is set, <i>false</i>
    /// otherwise.
    bool is_set_bloom_filter() const;

    /// Sets <i>in memory</i> option.
    /// Sets the IN_MEMORY bit of #m_isset and sets #m_in_memory to
    /// <code>value</code>.
    /// @param value New value for <i>in memory</i> option
    void set_in_memory(bool value);

    /// Gets <i>in memory</i> option.
    /// @return <i>in memory</i> option.
    bool get_in_memory() const { return m_in_memory; }

    /// Checks if <i>in memory</i> option is set.
    /// This method returns the value of the IN_MEMORY bit of #m_isset.
    /// @return <i>true</i> if <i>in memory</i> option is set, <i>false</i>
    /// otherwise.
    bool is_set_in_memory() const;

    /// Merges options from another AccessGroupOptions object.
    /// For each option that is not set, if the corresponding option in the
    /// <code>other</code> parameter is set, then the option is set to
    /// <code>other</code>'s value.
    /// @param other AccessGroupOptions object with which to merge
    void merge(const AccessGroupOptions &other);

    /** Parses XML options specification.
     * This method parses an XML document which holds access group options,
     * populates the corresponding member variables.  The parser accepts
     * any element name for the outtermost element so that it can be used
     * to parse options from any section within a schema XML document (e.g. 
     * AccessGroupDefaults or Options).  The following example illustrates a
     * valid XML opitons document.
     * @verbatim
     * <Options>
     *   <Replication>3</Replication>
     *   <BlockSize>67108864</BlockSize>
     *   <Compressor>zlib --best</Compressor>
     *   <BloomFilter>rows+cols --false-positive 0.02 --bits-per-item 9
     *                --num-hashes 7 --max-approx-items 900</BloomFilter>
     *   <InMemory>true</InMemory>
     * </Options>
     * @endverbatim
     * @param base Pointer to character buffer holding XML document
     * @param len Length of XML document
     */
    void parse_xml(const char *base, int len);

    /** Renders options in XML format.
     * Access group options can be specified in an XML schema document within
     * the <code>%AccessGroup -> Options</code> element or the
     * <code>%Table -> AccessGroupDefaults</code> element.  This member
     * function generates the XML option specification elements that can be
     * used in both places.  The <code>line_prefix</code> parameter can be used
     * to get nice indenting if the object is being rendered as part of a larger
     * object and is typically some number of space characters.  The following
     * is example output produced by this member function.
     * @verbatim
     *   <Replication>3</Replication>
     *   <BlockSize>67108864</BlockSize>
     *   <Compressor>zlib --best</Compressor>
     *   <BloomFilter>rows+cols --false-positive 0.02 --bits-per-item 9
     *                --num-hashes 7 --max-approx-items 900</BloomFilter>
     *   <InMemory>true</InMemory>
     * @endverbatim
     * @param line_prefix std::string to prepend to each line of output
     * @return std::string representing options in XML format
     */
    const std::string render_xml(const std::string &line_prefix) const;

    /// Renders options in HQL format.
    /// Access group options can be specified in an HQL
    /// <code>CREATE TABLE</code> command for access group specifications or
    /// table defaults.  In both situations, the format of the options
    /// specification is the same.  The following shows an example of the HQL
    /// output produced by this member function.
    /// <pre>
    /// REPLICATION 3 BLOCKSIZE 67108864 COMPRESSOR "zlib --best" BLOOMFILTER "rows+cols --false-positive 0.02" IN_MEMORY
    /// </pre>
    /// @return std::string representing options in HQL format
    const std::string render_hql() const;

    /// Parsers a bloom filter specification and sets properties.
    /// Parses the bloom filter specification given in <code>spec</code> and
    /// populates <code>props</code> with the corresponding properties described
    /// in the following table.
    /// <table>
    /// <tr>
    /// <th>%Property</th>
    /// <th>Type</th>
    /// <th>Default</th>
    /// <th>Description</th>
    /// </tr>
    /// <tr>
    /// <td>bloom-filter-mode</td>
    /// <td>string</td>
    /// <td><i>none</i></td>
    /// <td>Mode (rows|rows+cols|none)</td>
    /// </tr>
    /// <tr>
    /// <td>bits-per-item</td>
    /// <td>float</td>
    /// <td><i>none</i></td>
    /// <td>Number of bits to use per item</td>
    /// </tr>
    /// <tr>
    /// <td>num-hashes</td>
    /// <td>int</td>
    /// <td><i>none</i></td>
    /// <td>Number of hash functions to use</td>
    /// </tr>
    /// <tr>
    /// <td>false-positive</td>
    /// <td>float</td>
    /// <td>0.01</td>
    /// <td>Expected false positive probability</td>
    /// </tr>
    /// <tr>
    /// <td>max-approx-items</td>
    /// <td>int</td>
    /// <td>1000</td>
    /// <td>Number of cell store items used to estimate the number of actual
    /// entries</td>
    /// </tr>
    /// </table>
    /// @param spec Bloom filter specification
    /// @param props Properties object to populate
    static void parse_bloom_filter(const std::string &spec, PropertiesPtr &props);

    /// Equality operator.
    /// @param other Other object to which comparison is to be made
    /// @return <i>true</i> if this object is equal to <code>other</code>,
    /// <i>false</i> otherwise.
    bool operator==(const AccessGroupOptions &other) const;

  private:

    /// Replication
    int16_t m_replication {-1};

    /// Block size
    int32_t m_blocksize {};
    
    /// Compressor specification
    std::string m_compressor;

    /// Bloom filter specification
    std::string m_bloomfilter;

    /// In memory
    bool m_in_memory {};

    /// Bit mask describing which options are set
    std::bitset<MAX> m_isset;
  };

  /// Access group specification
  class AccessGroupSpec {
  public:

    /// Default constructor.
    AccessGroupSpec() { }

    /// Constructor with name initializer.
    /// Initializes #m_name with <code>name</code>
    /// @param name Access group name
    AccessGroupSpec(const std::string &name) : m_name(name) { }

    /// Destructor.
    /// Deletes all of the column family specification objects in #m_columns
    ~AccessGroupSpec();

    /// Sets access group name.
    /// Sets #m_name to <code>name</code>
    /// @param name Access group name
    void set_name(const std::string &name);

    /// Gets access group name.
    /// @return Access group name.
    const std::string &get_name() const { return m_name; }

    /// Sets generation.
    /// @param generation Generation value
    void set_generation(int64_t generation) { m_generation = generation; }

    /// Clears generation.
    /// Sets #m_generation to 0.
    void clear_generation() { m_generation = 0; }

    /// Clears generation if different than <code>original</code>.
    /// Compares this object with <code>original</code> and if then differ, sets
    /// #m_generation to 0 and returns <i>true</i>.  Comparison of each member
    /// column family is compared with
    /// ColumnFamilySpec::clear_generation_if_changed() causing the column
    /// family spec's generation to be set to 0 if they differ.
    /// @param original Original access group spec with which to compare
    /// @return <i>true</i> if this object differs from <code>original</code>,
    /// <i>false</i> otherwise.
    bool clear_generation_if_changed(AccessGroupSpec &original);

    /// Gets generation.
    /// @return Generation value
    int64_t get_generation() const { return m_generation; }

    /// Sets <i>replication</i> option.
    /// Sets the <i>replication</i> option of the #m_options member to
    /// <code>replication</code>
    /// @param replication New value for <i>replication</i> option
    void set_option_replication(int16_t replication);

    /// Gets <i>replication</i> option.
    /// @return <i>replication</i> option.
    int16_t get_option_replication() const;

    /// Sets <i>blocksize</i> option.
    /// Sets the <i>blocksize</i> option of the #m_options member to
    /// <code>blocksize</code>
    /// @param blocksize New value for <i>blocksize</i> option
    void set_option_blocksize(int32_t blocksize);

    /// Gets <i>blocksize</i> option.
    /// @return <i>blocksize</i> option.
    int32_t get_option_blocksize() const;

    /// Sets <i>compressor</i> option.
    /// Sets the <i>compressor</i> option of the #m_options member to
    /// <code>compressor</code> by calling AccessGroupOptions::set_compressor().
    /// @param compressor Compressor specification
    /// @throws Exception with code set to Error::SCHEMA_PARSE_ERROR
    /// if compressor specification is invalid
    void set_option_compressor(const std::string &compressor);

    /// Gets <i>compressor</i> option.
    /// @return <i>compressor</i> option.
    const std::string get_option_compressor() const;

    /// Sets <i>bloom filter</i> option.
    /// Sets the <i>bloom filter</i> option of the #m_options member to
    /// <code>bloomfilter</code> by calling
    /// AccessGroupOptions::set_bloom_filter().
    /// @param bloomfilter Bloom filter specification
    /// @throws Exception with code set to Error::SCHEMA_PARSE_ERROR
    /// if bloom filter specification is invalid
    void set_option_bloom_filter(const std::string &bloomfilter);

    /// Gets <i>bloom filter</i> option.
    /// @return <i>bloom filter</i> option.
    const std::string &get_option_bloom_filter() const;

    /// Sets <i>in memory</i> option.
    /// Sets the <i>in memory</i> option of the #m_options member to
    /// <code>value</code>
    /// @param value New value for <i>in memory</i> option
    void set_option_in_memory(bool value);

    /// Gets <i>in memory</i> option.
    /// @return <i>in memory</i> option.
    bool get_option_in_memory() const;

    /// Sets default <i>max versions</i> column family option.
    /// Sets <i>max versions</i> option in the column family default structure,
    /// #m_defaults, to <code>max_versions</code>
    /// @param max_versions New <i>max versions</i> column family default
    /// @throws Exception if <code>max_versions</code> is negative or if the
    /// <i>counter</i> default option is set, which is incompatible.
    void set_default_max_versions(int32_t max_versions);

    /// Gets default <i>max versions</i> column family option.
    /// @return Default <i>max versions</i> column family option.
    int32_t get_default_max_versions() const;

    /// Sets default <i>ttl</i> column family option.
    /// Sets <i>ttl</i> option in the column family default structure,
    /// #m_defaults, to <code>ttl</code>
    /// @param ttl New <i>ttl</i> column family default
    /// @throws Exception if supplied <code>ttl</code> value is negative.
    void set_default_ttl(time_t ttl);

    /// Gets default <i>ttl</i> column family option.
    /// @return Default <i>ttl</i> column family option.
    time_t get_default_ttl() const;

    /// Sets default <i>time order desc</i> column family option.
    /// Sets <i>time order desc</i> option in the column family default
    /// structure, #m_defaults, to <code>value</code>
    /// @param value New <i>time order desc</i> column family default
    /// @throws Exception if supplied <code>value</code> is <i>true</i> and
    /// the <i>counter</i> default option is set, which is incompatible with the
    /// <i>time order desc</i> default option.
    void set_default_time_order_desc(bool value);

    /// Gets default <i>time order desc</i> column family option.
    /// @return Default <i>time order desc</i> column family option.
    bool get_default_time_order_desc() const;

    /// Sets default <i>counter</i> column family option.
    /// Sets <i>counter</i> option in the column family default structure,
    /// #m_defaults, to <code>value</code>
    /// @param value New <i>counter</i> column family default
    /// @throws Exception if supplied <code>value</code> is <i>true</i> and
    /// either the <i>max versions</i> or <i>time order desc</i> default
    /// options are set, which are incompatible with the <i>counter</i> default
    /// option.
    void set_default_counter(bool value);

    /// Gets default <i>counter</i> column family option.
    /// @return Default <i>counter</i> column family option.
    bool get_default_counter() const;

    /// Adds column family specification.
    /// Merges column family defaults, #m_defaults, into <code>cf</code>
    /// options, sets the <code>cf</code> access group to #m_name, and then
    /// pushes, <code>cf</code>, onto the end of #m_columns.
    void add_column(ColumnFamilySpec *cf);

    /// Replaces column family specification.
    /// Merges default column family options into <code>new_cf</code> and then
    /// removes the old specification with the same name from #m_columns and
    /// adds <code>new_cf</code> to #m_columns.
    /// @param new_cf Replacement column family specification
    /// @return Old column family specification
    ColumnFamilySpec *replace_column(ColumnFamilySpec *new_cf);

    /// Removes column family specification.
    /// Removes column family specification with name <code>name</code> from
    /// #m_columns.
    /// @param name Name of column family specification to remove
    /// @return Removed column family specification
    ColumnFamilySpec *remove_column(const std::string &name);

    /// Drops column family.
    /// Finds column family specification with name <code>name</code> in
    /// #m_columns and clears its generation, marks it as deleted, and renames
    /// it to its column ID prefixed with the '!' character.
    void drop_column(const std::string &name);

    /// Gets column family specification.
    /// @return %Column specification matching <code>name</code> or nullptr if
    /// it is not contained in the spec
    ColumnFamilySpec *get_column(const std::string &name);

    /// Clears columns.
    /// Clears the #m_columns vector.
    void clear_columns() { m_columns.clear(); }

    /// Merges options with those from another AccessGroupOptions object.
    /// @param options Options with which to merge
    void merge_options(const AccessGroupOptions &options) {
      m_options.merge(options);
    }

    /// Merges column family defaults with those from another AccessGroupOptions
    /// object.
    /// @param options Options with which to merge
    void merge_defaults(const ColumnFamilyOptions &options) {
      m_defaults.merge(options);
    }

    /// Equality operator.
    /// @param other Other object to which comparison is to be made
    /// @return <i>true</i> if this object is equal to <code>other</code>,
    /// <i>false</i> otherwise.
    bool operator==(const AccessGroupSpec &other) const;

    /** Parses XML access group specification.
     * This method parses an XML document representing an access group
     * specification, populating the corresponding member variables.
     * @param base Pointer to character buffer holding XML document
     * @param len Length of XML document
     * @see parse_xml() for an example input that is parseable by this member
     * function.
     */
    void parse_xml(const char *base, int len);

    /** Renders access group specification in XML format.
     * Renders the specification as an XML document.  The
     * <code>line_prefix</code> parameter can be used to get nice indenting if
     * the object is being rendered as part of a larger object and is typically
     * some number of space characters.  The <code>with_ids</code> parameter
     * causes the generated XML to include the ID attribute and Generation
     * elements for the column families.  The following is example output
     * produced by this member function.
     * @verbatim
     <AccessGroup name="default">
       <Options>
         <Replication>3</Replication>
         <BlockSize>67108864</BlockSize>
         <Compressor>bmz --fp-len 20 --offset 5</Compressor>
         <BloomFilter>none</BloomFilter>
         <InMemory>true</InMemory>
       </Options>
       <ColumnFamilyDefaults>
         <MaxVersions>3</MaxVersions>
         <TTL>86400</TTL>
         <TimeOrder>desc</TimeOrder>
       </ColumnFamilyDefaults>
       <ColumnFamily id="42">
         <Generation>123</Generation>
         <Name>foo</Name>
         <AccessGroup>default</AccessGroup>
         <Deleted>false</Deleted>
         <Index>true</Index>
         <QualifierIndex>true</QualifierIndex>
         <Options>
           <MaxVersions>1</MaxVersions>
           <TTL>86400</TTL>
           <TimeOrder>desc</TimeOrder>
         </Options>
       </ColumnFamily>
       <ColumnFamily id="43">
         <Generation>234</Generation>
         <Name>bar</Name>
         <AccessGroup>default</AccessGroup>
         <Deleted>true</Deleted>
         <QualifierIndex>true</QualifierIndex>
         <Options>
           <MaxVersions>3</MaxVersions>
           <TTL>86400</TTL>
           <TimeOrder>desc</TimeOrder>
         </Options>
       </ColumnFamily>
     </AccessGroup>
     @endverbatim
     * @param line_prefix std::string to prepend to each line of output
     * @param with_ids Include column family IDs and Generation in output
     * @return std::string representing spec in XML format
     */
    const std::string render_xml(const std::string &line_prefix,
                                 bool with_ids=false) const;

    /// Renders access group specification in HQL format.
    /// Renders the access group specification in a format that can be used as
    /// the column specification in an HQL <code>CREATE TABLE</code> command.
    /// The following shows an example of the HQL produced by this member
    /// function (newlines added for readability).
    /// <pre>
    ///   ACCESS GROUP default (foo, baz) REPLICATION 3 BLOCKSIZE 67108864
    ///     COMPRESSOR "bmz --fp-len 20 --offset 5" BLOOMFILTER "none"
    ///     IN_MEMORY MAX_VERSIONS 3 TTL 86400 TIME_ORDER desc
    /// </pre>
    /// @return std::string representing access group specification in HQL format
    const std::string render_hql() const;

    /// Returns reference to column specifications.
    /// @return Reference to column specifications.
    ColumnFamilySpecs &columns() { return m_columns; }

    /// Returns reference to options structure.
    /// @return Reference to options structure.
    const AccessGroupOptions &options() const { return m_options; }

    /// Returns reference to column family defaults structure.
    /// @return Reference to column family defaults structure.
    const ColumnFamilyOptions &defaults() const { return m_defaults; }

  private:

    /// Name
    std::string m_name;

    /// Generation
    int64_t m_generation {};

    /// Options
    AccessGroupOptions m_options;

    /// Column family defaults
    ColumnFamilyOptions m_defaults;

    /// Member column family specifications
    ColumnFamilySpecs m_columns;
  };

  /// Vector of AccessGroupSpec pointers
  typedef std::vector<AccessGroupSpec *> AccessGroupSpecs;

  /// @}

} // namespace Hypertable

#endif // Hypertable_Lib_AccessGroup_h
