#!/usr/bin/env ruby
require 'rubygems'
require 'yaml'
require 'syck'
require 'json'
require 'titleize'
require 'sinatra/base'
require 'pathname'

%w(helpers).each {  |r| require "#{  File.dirname(__FILE__)}/app/#{r}"}
Dir["#{  File.dirname(__FILE__)}/app/lib/*.rb"].each {|r| require r}

if RUBY_VERSION >= "1.9"
  YAML::ENGINE.yamler = 'syck'
end

module HTMonitoring
  @root = Pathname.new(File.dirname(__FILE__)).expand_path
  @hypertable_home = ENV['HYPERTABLE_HOME']

  def self.config
    @config ||= YAML.load_file(@root.join("app/config/config.yml"))[:production]
    @config[:data] = eval "\"#{@config[:data]}\""
    @config
  end

  def self.tablestats
    @tablestats ||= YAML.load_file(@root.join("app/config/tablestats.yml"))[:stats]
  end

  def self.rangeserverstats
    @rangeserverstats ||= YAML.load_file(@root.join("app/config/rangeserverstats.yml"))[:stats]
  end

  def self.rrdstats
    @rrdstats ||= YAML.load_file(@root.join("app/config/rrdstats.yml"))[:stats]
  end

  class Admin < Sinatra::Base
    @root = Pathname.new(File.dirname(__FILE__)).expand_path
    set :environment, :production
    set :public, @root.join('app/public')
    set :views,  @root.join('app/views')
    set :logging, true

    enable :static
    disable :run

    helpers do
      include HTMonitoring::Helpers
    end

    get '/' do
      @range_servers = StatsJson.new(:file => 'rangeserver_summary.json')
      @rs_records = @range_servers.parse_stats_file
      @master = StatsJson.new(:file => 'master_summary.json')
      @master_record = @master.parse_stats_file
      @rs_records['RangeServerSummary']['servers'].sort! { |a,b| a['order'].to_i <=> b['order'].to_i }
      @n_ord="dsc"
      @name_sort_img = "/images/arrow_up.png"
      if params[:sort] == "name" && params[:ord] = "dsc"
        @rs_records['RangeServerSummary']['servers'].sort! { |a,b| b['order'].to_i <=> a['order'].to_i }
        @n_ord="asc"
        @name_sort_img = "/images/arrow_down.png"
      end
      erb :index
    end

    get '/tables' do
      @tables = StatsJson.new(:file => 'table_summary.json')
      @table_records = @tables.parse_stats_file
      @master = StatsJson.new(:file => 'master_summary.json')
      @master_record = @master.parse_stats_file
      
      @id_sort_img = "/images/arrows.png"
      @id_ord = "asc"
      @table_records['TableSummary']['tables'].sort! { |a,b| a['name'] <=> b['name'] }
      @n_ord="dsc"
      @name_sort_img = "/images/arrow_up.png"
      
      if params[:sort] == "name" && params[:ord] = "dsc"
        @table_records['TableSummary']['tables'].sort! { |a,b| b['name'] <=> a['name'] }
        @n_ord="asc"
        @name_sort_img = "/images/arrow_down.png"
      elsif params[:sort] == "id" && params[:ord] == "asc"
        @table_records['TableSummary']['tables'].sort! { |a,b| a['id'] <=> b['id'] }
        @id_ord = "dsc"
        @id_sort_img = "/images/arrow_up.png"
      elsif params[:sort] == "id" && params[:ord] == "dsc"
        @table_records['TableSummary']['tables'].sort! { |a,b| b['id'] <=> a['id'] }
        @id_ord = "asc"
        @id_sort_img = "/images/arrow_asc.png"
      end
      erb :tables
    end

    get '/graphs' do
      @master = StatsJson.new(:file => 'master_summary.json')
      @master_record = @master.parse_stats_file
      @server = params[:server]
      @stype = params[:type]
      @start_time = params[:start_time]
      @end_time = params[:end_time]
      if @stype.nil?
        @stype = "RangeServer"
      end
      erb :graphs
    end

    get '/mop_graph' do
        file_name = HTMonitoring.config[:data]+"mop.svg"
        content_type 'image/svg+xml'
        if File.exists?(file_name) 
           file = File.open(file_name)
           contents=""
           file.each {|line|
             contents << line
           }
          contents
        end
    end

    error do
      erb :error
    end

    get '/data/:server/:key/:type' do
      if params[:server].downcase == "rangeserver" and params[:key].downcase == "servers"
        content_type :js
        rrd_stats = RRDStat.new
        json = rrd_stats.get_server_list
        graph_callback(json)
      elsif params[:server].downcase == "table"
        content_type :js
        rrd_stats = RRDStat.new
        json = rrd_stats.get_table_list
        graph_callback(json)
      end
    end

    get '/graph/:type/:server/:stat/:starttime/:endtime' do
      content_type 'image/gif'
      if params[:type].downcase == "rangeserver"
        rrd_stats = RRDStat.new
        image_data = rrd_stats.get_rrd_stat_image params[:server],params[:stat],params[:starttime],params[:endtime]
      elsif params[:type].downcase == "table"
        rrd_stats = RRDStat.new
        image_data = rrd_stats.get_table_stat_image params[:server],params[:stat],params[:starttime],params[:endtime]
      end
    end

     get '/data/:type/:stat/:timestamp_index/:sort_by/:resolution' do
      if params[:type].downcase == "table"
        content_type :js
        stats = TableStats.new
        json = stats.get_graph_data({:stat => params[:stat], :timestamp_index => params[:timestamp_index].to_i, :sort_by => params[:sort_by]})
        graph_callback(json)
      elsif params[:type].downcase == "rangeserver"
        content_type :js
        stats = RRDStat.new
        json = stats.get_graph_data({:stat => params[:stat], :timestamp_index => params[:timestamp_index].to_i, :sort_by => params[:sort_by],:resolution => params[:resolution]})
        graph_callback(json)
      end
    end

    def graph_callback(json)
      params[:callback] ? params[:callback] + '(' + json + ')' : json
    end

  end

end
