# ec-bokeh
bgfx example style project for bokeh depth of field

# building
1) setup bgfx
2) add these files to new folder in 'examples', like 'examples\xx-bokeh'
3) edit 'scripts\genie.lua' and 'examples\makefile' to add this new example to list of examples
4) run makefile in this folder to compile shaders, like 'make TARGET=1' to compile dx11 shaders

# notes
Implement bokeh depth of field as described in the blog post here:
https://blog.tuxedolabs.com/2018/05/04/bokeh-depth-of-field-in-single-pass.html

Additionally, implement the optimizations discussed in the closing paragraph. Apply the effect in multiple passes. Calculate the circle of confusion and store in the alpha channel while downsampling the image. Then compute depth of field at this lower res, storing sample size in alpha. Then composite the blurred image, based on the sample size. Compositing the lower res like this can lead to blocky edges where there's a depth discontinuity and the blur is just enough. May be an area to improve on.

Provide an alternate means of determining radius of current sample when blurring. I find the blog post's sample pattern to be difficult to directly reason about. It is not obvious, given the parameters, how many samples will be taken. And it can be very many samples. Though the results are good. The 'sqrt' pattern chosen here looks alright and allows for the number of samples to be set directly. If you are going to use this in a project, may be worth exploring additional sample patterns. And certainly update the shader to remove the pattern choice from inside the sample loop.
