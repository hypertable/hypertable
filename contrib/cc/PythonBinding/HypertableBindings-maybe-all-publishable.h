#ifndef _HAVE_HYPERTABLE_BINDINGS
#define _HAVE_HYPERTABLE_BINDINGS

#include <Hypertable/Lib/Client.h>
#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>
#include <map>


#include <pybind11/pybind11.h>
using namespace Hypertable;
namespace py = pybind11;


class PyKeySpec {
private:
	KeySpec m_key;
	std::string m_key_str;
	std::string m_column_qualifier;
	std::string m_column_family;

public:
	PyKeySpec(std::string& key, std::string column_family, std::string column_qualifier)
	{
		m_key_str = key;
		m_column_qualifier = column_qualifier;
		m_column_family = column_family;

		m_key.row_len = m_key_str.size();
		if (m_key_str.size() > 0)
			m_key.row = m_key_str.c_str();

		m_key.column_family = column_family.c_str();

		m_key.column_qualifier_len = column_qualifier.size();
		if (column_qualifier.size() > 0)
			m_key.column_qualifier = column_qualifier.c_str();
	}

	KeySpec& compile() { return m_key; }
};


class PyTableMutator {
private:
	TableMutatorPtr m_mutator;

public:
	PyTableMutator() {}
	PyTableMutator(TableMutatorPtr mutator) : m_mutator(mutator)
	{
	}

	void set(std::string row, std::string column, std::string qualifier, const char *val, unsigned int value_len)
	{
		PyKeySpec k(row, column, qualifier);
		m_mutator->set(k.compile(), val, value_len);
	}

	void flush() { m_mutator->flush(); }
};

std::string cell_value(Cell& c)
{
	char *tmp = new char[c.value_len + 1];
	memcpy(tmp, c.value, c.value_len);
	tmp[c.value_len] = 0;

	std::string ret(tmp);
	delete[] tmp;
	return ret;
}
class PyClient {
private:
	Client *m_client;

public:
	PyClient()
	{
		m_client = new Client();
	}

	TablePtr open_table(const std::string& ns, const std::string& name)
	{
		return m_client->open_namespace(ns)->open_table(name);
	}

	void create_table(const std::string& ns, const std::string& name, const std::string& schema)
	{
		m_client->open_namespace(ns)->create_table(name, schema);
	}
};




PYBIND11_MODULE(ht, m) {
	m.doc() = "pyht: libHT for PyPy";

	py::class_<PyClient>(m, "Client")
		.def(py::init<>())
		.def("open_table", &PyClient::open_table)
		.def("create_table", &PyClient::create_table)
		;
}

#endif

