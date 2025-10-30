#ifndef VEC3_H
#define VEC3_H


class vec3 {
    public:
        double e[3];

        //constructor initializer list for e0,1,2
        vec3(double e0 = 0, double e1 = 0, double e2 = 0) : e{e0, e1, e2} {}

        //getter functions e.g v.x() easier than v.e[0]
        double x() const { return e[0]; }
        double y() const { return e[1]; }
        double z() const { return e[2]; }

        //operator overloads for -, [], +=, *=, /=
        vec3 operator-() const { return vec3(-e[0], -e[1], -e[2]); }
        double operator[](int i) const { return e[i]; }
        double& operator[](int i) { return e[i]; }

        //using & to return reference to current object to allow chaining
        vec3& operator+=(const vec3& v){
            e[0] += v.e[0];
            e[1] += v.e[1];
            e[2] += v.e[2];
            return *this;
        }

        vec3& operator*=(double t){
            e[0] *= t;
            e[1] *= t;
            e[2] *= t;
            return *this;
        }

        vec3& operator/=(double t){
            return *this *= 1/t;
        }

        //length functions
        double length() const {
            return std::sqrt(length_squared());
        }

        double length_squared() const {
            return e[0]*e[0] + e[1]*e[1] + e[2]*e[2];
        }

};

//type alias for vec3
using point3 = vec3;   //3D point

//vec3 utility functions
inline std::ostream& operator<<(std::ostream& out, const vec3& v) {
    return out << v.e[0] << ' ' << v.e[1] << ' ' << v.e[2];
}

//+ operator
inline vec3 operator+(const vec3& u, const vec3& v){
    return vec3(u.e[0] + v.e[0], u.e[1] + v.e[1], u.e[2] + v.e[2]);
}

//- operator
inline vec3 operator-(const vec3& u, const vec3& v){
    return vec3(u.e[0] - v.e[0], u.e[1] - v.e[1], u.e[2] - v.e[2]);
}

//*.* operator overloads for vec3
//   vec3 * vec3 (component-wise multiplication)
//   double * vec3
//   vec3 * double

inline vec3 operator*(const vec3& u, const vec3& v){
    return vec3(u.e[0] * v.e[0], u.e[1] * v.e[1], u.e[2] * v.e[2]);
}

inline vec3 operator*(double t, const vec3& v){
    return vec3(t*v.e[0], t*v.e[1], t*v.e[2]);
}

inline vec3 operator*(const vec3& v, double t){
    return t * v;
}

inline vec3 operator/(const vec3& v, double t){
    return (1/t) * v;
}

//dot product and cross product functions
inline double dot(const vec3& u, const vec3& v){
    return u.e[0] * v.e[0]
         + u.e[1] * v.e[1]
         + u.e[2] * v.e[2];
}

inline vec3 cross(const vec3& u, const vec3& v){
    return vec3(u.e[1] * v.e[2] - u.e[2] * v.e[1],
                u.e[2] * v.e[0] - u.e[0] * v.e[2],
                u.e[0] * v.e[1] - u.e[1] * v.e[0]);
}

//unit vector function
inline vec3 unit_vector(const vec3& v){
    return v / v.length();
}

// VEC3_H
#endif 