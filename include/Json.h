#ifndef JSON_H
#define JSON_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>

// Escritor JSON mínimo para entregar los resultados a la interfaz (app.py)
class Json {
private:
    std::ostream& os;
    std::vector<bool> primero;
    std::vector<char> cierres;

    void separador(const char* clave) {
        if (!primero.empty()) {
            if (!primero.back()) os << ',';
            primero.back() = false;
        }
        if (clave) {
            texto(clave);
            os << ':';
        }
    }

    void texto(const std::string& s) {
        os << '"';
        for (unsigned char c : s) {
            if (c == '"') os << "\\\"";
            else if (c == '\\') os << "\\\\";
            else if (c == '\n') os << "\\n";
            else if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                os << buf;
            } else os << c;
        }
        os << '"';
    }

public:
    explicit Json(std::ostream& os) : os(os) { os.precision(10); }

    Json& objeto(const char* clave = nullptr) {
        separador(clave);
        os << '{';
        primero.push_back(true);
        cierres.push_back('}');
        return *this;
    }

    Json& arreglo(const char* clave = nullptr) {
        separador(clave);
        os << '[';
        primero.push_back(true);
        cierres.push_back(']');
        return *this;
    }

    Json& fin() {
        os << cierres.back();
        cierres.pop_back();
        primero.pop_back();
        return *this;
    }

    Json& num(const char* clave, double v) {
        separador(clave);
        if (std::isfinite(v)) os << v;
        else os << "null";
        return *this;
    }

    Json& ent(const char* clave, int64_t v) {
        separador(clave);
        os << v;
        return *this;
    }

    Json& str(const char* clave, const std::string& v) {
        separador(clave);
        texto(v);
        return *this;
    }

    Json& booleano(const char* clave, bool v) {
        separador(clave);
        os << (v ? "true" : "false");
        return *this;
    }
};

#endif
