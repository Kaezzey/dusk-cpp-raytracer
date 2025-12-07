#ifndef TEXTURE_H
#define TEXTURE_H

#include "dusk_image.h"
#include "perlin.h"
#include "dusk_image.h"

class texture {
  public:
    virtual ~texture() = default;
    virtual colour value(double u, double v, const point3& p) const = 0;
    // Alpha sampling: return alpha in [0,1]. Default = 1 (opaque)
    virtual double alpha_at(double u, double v, const point3& p) const { return 1.0; }
};

class solid_colour : public texture {
  public:
    solid_colour(const colour& albedo) : albedo(albedo) {}

    solid_colour(double red, double green, double blue) : solid_colour(colour(red,green,blue)) {}

    colour value(double u, double v, const point3& p) const override {
        return albedo;
    }

  private:
    colour albedo;
};

class checker_texture : public texture {
  public:
    checker_texture(double scale, shared_ptr<texture> even, shared_ptr<texture> odd)
      : inv_scale(1.0 / scale), even(even), odd(odd) {}

    checker_texture(double scale, const colour& c1, const colour& c2)
      : checker_texture(scale, make_shared<solid_colour>(c1), make_shared<solid_colour>(c2)) {}

    colour value(double u, double v, const point3& p) const override {
        auto xInteger = int(std::floor(inv_scale * p.x()));
        auto yInteger = int(std::floor(inv_scale * p.y()));
        auto zInteger = int(std::floor(inv_scale * p.z()));

        bool isEven = (xInteger + yInteger + zInteger) % 2 == 0;

        return isEven ? even->value(u, v, p) : odd->value(u, v, p);
    }

  private:
    double inv_scale;
    shared_ptr<texture> even;
    shared_ptr<texture> odd;
};

class noise_texture : public texture {
  public:
    noise_texture(double scale) : scale(scale) {}

    colour value(double u, double v, const point3& p) const override {
        return colour(.5, .5, .5) * (1 + std::sin(scale * p.z() + 10 * noise.turb(p, 7)));
    }

  private:
    perlin noise;
    double scale;
};

class image_texture : public texture {
  public:
    image_texture(const char* filename) : image(filename) {}

    // Expose whether the loaded image contains an alpha channel.
    bool has_alpha() const { return image.has_alpha(); }

    colour value(double u, double v, const point3& p) const override {
        //no texture data, then return solid cyan as a debugging aid
        if (image.height() <= 0) return colour(0,1,1);

        //clamp input texture coordinates to [0,1] x [1,0]
        u = interval(0,1).clamp(u);

        //flip V to image coordinates
        v = 1.0 - interval(0,1).clamp(v);  

        auto i = int(u * image.width());
        auto j = int(v * image.height());
        auto pixel = image.pixel_data(i,j);

        auto color_scale = 1.0 / 255.0;
        return colour(color_scale*pixel[0], color_scale*pixel[1], color_scale*pixel[2]);
    }

      double alpha_at(double u, double v, const point3& p) const override {
        if (image.height() <= 0) return 1.0;

        u = interval(0,1).clamp(u);
        v = 1.0 - interval(0,1).clamp(v);

        auto i = int(u * image.width());
        auto j = int(v * image.height());
        if (!image.has_alpha()) return 1.0;
        auto a = image.pixel_alpha_byte(i, j);
        return double(a) / 255.0;
      }

  private:
    rtw_image image;
};

#endif